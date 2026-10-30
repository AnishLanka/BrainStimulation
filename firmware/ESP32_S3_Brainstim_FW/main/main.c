/* ESP32-S3 BLE NUS — I2C DDS Master Controller
 *
 * Implements a BLE GATT peripheral (Nordic UART Service) that acts as a
 * wireless command interface to RP2040 Zero DDS slave MCUs connected via I2C.
 *
 * Service / Characteristic UUIDs (Nordic UART Service):
 * Service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 * RX char  6E400002-B5A3-F393-E0A9-E50E24DCCA9E  (central writes here)
 * TX char  6E400003-B5A3-F393-E0A9-E50E24DCCA9E  (ESP32 notifies here)
 *
 * BLE DDS command set (write to RX):
 * DDS <n> FREQ <hz>                Set frequency for slave n (0-3)
 * DDS <n> WAVE SINE|SQUARE|TRIANGLE|SAWTOOTH
 * DDS <n> AMP <mA>                 Set amplitude for slave n
 * DDS <n> ON / OFF                 Enable / disable slave n
 * DDS <n> STATUS                   Read live status from slave n over I2C
 * DDS ALL FREQ|WAVE|AMP|ON|OFF|STATUS   Broadcast to all slaves
 *
 * Current test config: 1 RP2040 Zero slave at I2C address 0x50 (slave 0).
 * Four channels at 0x50-0x53 are supported.
 *
 * Companion web client: web/ble_test.html (Web Bluetooth).
 */

/* Set to 1 for Chrome desktop testing (no BLE pairing/encryption).
 * Set to 0 for Android production (full MITM-protected secure connection).
 * !! NEVER ship or use near high-power hardware with this set to 1 !!      */
#define BLE_TEST_NO_SECURITY  1


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "gpio_pins.h"
#include "i2c_dds_master.h"
#include "oled_driver.h"
#include "fsm.h"
#include "gpio_helpers.h"

/* NimBLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* ESP-IDF */
#include "esp_random.h"
#include "esp_pm.h"

/*GPIO */
#include "driver/gpio.h"


/*BLE*/
#define TAG         "BLE_NUS"
#define DEVICE_NAME "R2D2"

/* Hardware safety stays active; host tests mock peripherals. */

/* ── NUS UUIDs ───────────────────────────────────────────────────────── */
static const ble_uuid128_t NUS_SERVICE_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static const ble_uuid128_t NUS_RX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static const ble_uuid128_t NUS_TX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/* ── State ─────────────────────────────────────────────────────────────── */
static uint8_t  s_own_addr_type;
static uint16_t s_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_char_handle;
static bool     s_notify_enabled = false;
static bool     s_conn_params_retry;

static dds_master_t s_dds;

/* Populated at boot if DDS init fails; delivered to the BLE client on
 * first subscribe so the fault is visible even though BLE wasn't up yet. */
static char s_boot_fault_msg[64];

/* INCREASED QUEUE SIZE: Prevents commands from dropping during rapid bursts */
#define DDS_CMD_QUEUE_LEN  16
#define DDS_CMD_MAX_LEN    252 

typedef struct {
    char sub[DDS_CMD_MAX_LEN + 1];
} dds_cmd_msg_t;

static QueueHandle_t s_dds_cmd_queue;

static void ble_advertise(void);
static void ble_notify_str(const char *msg);

static void ble_notify_str(const char *msg)
{
    if (!s_notify_enabled || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
    if (om) {
        ble_gatts_notify_custom(s_conn_handle, s_tx_char_handle, om);
        ESP_LOGI(TAG, "TX -> \"%s\"", msg);
    }
}

/* Failed I2C commands leave output unconfirmed. */
static void report_slave_fault(uint8_t idx, esp_err_t err)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "DDS ERR S%d I2C %s; output unconfirmed",
             idx, esp_err_to_name(err));
    ble_notify_str(msg);
    fsm_handle_event(EV_CHANNEL_FAULT);
}

/* Reject empty, non-finite, overflowing and partially parsed numeric commands. */
static bool parse_dds_value(const char *text, float min, float max, float *out)
{
    char *end;
    errno = 0;
    float value = strtof(text, &end);
    if (end == text || errno == ERANGE || !isfinite(value)) return false;
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0' || value < min || value > max) return false;
    *out = value;
    return true;
}

static void process_dds_command(const char *sub)
{
    static const char *wnames[] = {"SINE", "SQUARE", "TRIANGLE", "SAWTOOTH"};
    char resp[180];
    esp_err_t err = ESP_OK;

    if (!s_dds.initialized) {
        ble_notify_str("DDS ERR driver not initialized");
        return;
    }

    uint8_t slave_idx = 0;
    bool    broadcast = false;
    const char *cmd;

    if (strncmp(sub, "ALL ", 4) == 0) {
        broadcast = true;
        cmd = sub + 4;
    } else if (sub[0] >= '0' && sub[0] <= '3' && sub[1] == ' ') {
        slave_idx = (uint8_t)(sub[0] - '0');
        if (!dds_master_slave_present(&s_dds, slave_idx)) {
            snprintf(resp, sizeof(resp), "DDS ERR S%d not present on bus", slave_idx);
            ble_notify_str(resp);
            return;
        }
        cmd = sub + 2;
    } else {
        ble_notify_str("DDS ERR use 'DDS <0-3> CMD' or 'DDS ALL CMD'");
        return;
    }

    /* ── FREQ ── */
    if (strcmp(cmd, "STATUS") && strcmp(cmd, "OFF") && !fsm_commands_allowed()) {
        ble_notify_str("DDS ERR stimulation locked; stop/fault requires restart");
        return;
    }
    if (broadcast) {
        bool any = false;
        for (uint8_t i = 0; i < DDS_NUM_SLAVES; ++i)
            any |= dds_master_slave_present(&s_dds, i);
        if (!any && strcmp(cmd, "STATUS")) {
            ble_notify_str("DDS ERR no cards present"); return;
        }
    }
    if (strncmp(cmd, "FREQ ", 5) == 0) {
        float freq;
        if (!parse_dds_value(cmd + 5, DDS_MIN_FREQ_HZ, DDS_MAX_FREQ_HZ, &freq)) {
            snprintf(resp, sizeof(resp), "DDS ERR FREQ range %.3f-%.0f Hz",
                     DDS_MIN_FREQ_HZ, DDS_MAX_FREQ_HZ);
            ble_notify_str(resp);
            return;
        }
        if (broadcast) {
            for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
                if (!dds_master_slave_present(&s_dds, i)) continue;
                esp_err_t e = dds_master_set_freq(&s_dds, i, freq);
                if (e != ESP_OK) { report_slave_fault(i, e); err = e; }
            }
            if (err == ESP_OK) snprintf(resp, sizeof(resp), "DDS OK ALL FREQ=%.3f", freq);
        } else {
            err = dds_master_set_freq(&s_dds, slave_idx, freq);
            snprintf(resp, sizeof(resp), "DDS OK S%d FREQ=%.3f", slave_idx, freq);
        }

    /* ── WAVE ── */
    } else if (strncmp(cmd, "WAVE ", 5) == 0) {
        const char *wname = cmd + 5;
        dds_waveform_t wtype;
        if      (strcmp(wname, "SINE")     == 0) wtype = DDS_WAVE_SINE;
        else if (strcmp(wname, "SQUARE")   == 0) wtype = DDS_WAVE_SQUARE;
        else if (strcmp(wname, "TRIANGLE") == 0) wtype = DDS_WAVE_TRIANGLE;
        else if (strcmp(wname, "SAWTOOTH") == 0) wtype = DDS_WAVE_SAWTOOTH;
        else {
            ble_notify_str("DDS ERR WAVE use SINE|SQUARE|TRIANGLE|SAWTOOTH");
            return;
        }
        if (broadcast) {
            for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
                if (!dds_master_slave_present(&s_dds, i)) continue;
                esp_err_t e = dds_master_set_waveform(&s_dds, i, wtype);
                if (e != ESP_OK) { report_slave_fault(i, e); err = e; }
            }
            if (err == ESP_OK) snprintf(resp, sizeof(resp), "DDS OK ALL WAVE=%s", wnames[wtype]);
        } else {
            err = dds_master_set_waveform(&s_dds, slave_idx, wtype);
            snprintf(resp, sizeof(resp), "DDS OK S%d WAVE=%s", slave_idx, wnames[wtype]);
        }

    /* ── AMP ── */
    } else if (strncmp(cmd, "AMP ", 4) == 0) {
        float amp;
        if (!parse_dds_value(cmd + 4, 0.0f, DDS_MAX_AMPLITUDE_MA, &amp)) {
            snprintf(resp, sizeof(resp), "DDS ERR AMP range 0-%.0f mA",
                     DDS_MAX_AMPLITUDE_MA);
            ble_notify_str(resp);
            return;
        }
        if (broadcast) {
            for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
                if (!dds_master_slave_present(&s_dds, i)) continue;
                esp_err_t e = dds_master_set_amplitude(&s_dds, i, amp);
                if (e != ESP_OK) { report_slave_fault(i, e); err = e; }
            }
            if (err == ESP_OK) snprintf(resp, sizeof(resp), "DDS OK ALL AMP=%.3f", amp);
        } else {
            err = dds_master_set_amplitude(&s_dds, slave_idx, amp);
            snprintf(resp, sizeof(resp), "DDS OK S%d AMP=%.3f", slave_idx, amp);
        }

    /* ── ON ── */
    } else if (strcmp(cmd, "ON") == 0) {
        /* Check readiness before starting any member of a broadcast. */
        for (uint8_t i = broadcast ? 0 : slave_idx;
             i < (broadcast ? DDS_NUM_SLAVES : slave_idx + 1); ++i) {
            if (!dds_master_slave_present(&s_dds, i)) continue;
            if (!fsm_channel_ready(i)) {
                snprintf(resp, sizeof(resp),
                         "DDS ERR S%d not ready; check HV configuration and card status", i);
                ble_notify_str(resp);
                return;
            }
        }
        if (broadcast) {
            for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
                if (!dds_master_slave_present(&s_dds, i)) continue;
                esp_err_t e = fsm_channel_ready(i) ? dds_master_set_enable(&s_dds, i, true) : ESP_ERR_INVALID_STATE;
                if (e != ESP_OK) { report_slave_fault(i, e); err = e; }
            }
            if (err == ESP_OK) strcpy(resp, "DDS OK ALL ON");
        } else {
            err = fsm_channel_ready(slave_idx) ? dds_master_set_enable(&s_dds, slave_idx, true) : ESP_ERR_INVALID_STATE;
            snprintf(resp, sizeof(resp), "DDS OK S%d ON", slave_idx);
        }

    /* ── OFF ── */
    } else if (strcmp(cmd, "OFF") == 0) {
        if (broadcast) {
            for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
                if (!dds_master_slave_present(&s_dds, i)) continue;
                esp_err_t e = dds_master_set_enable(&s_dds, i, false);
                if (e != ESP_OK) { report_slave_fault(i, e); err = e; }
            }
            if (err == ESP_OK) strcpy(resp, "DDS OK ALL OFF");
        } else {
            err = dds_master_set_enable(&s_dds, slave_idx, false);
            snprintf(resp, sizeof(resp), "DDS OK S%d OFF", slave_idx);
        }

    /* ── STATUS ── */
    } else if (strcmp(cmd, "STATUS") == 0) {
        uint8_t start = broadcast ? 0        : slave_idx;
        uint8_t end   = broadcast ? DDS_NUM_SLAVES : slave_idx + 1;
        for (uint8_t i = start; i < end; i++) {
            if (!dds_master_slave_present(&s_dds, i)) {
                snprintf(resp, sizeof(resp), "DDS S%d NOT_PRESENT", i);
                ble_notify_str(resp); continue;
            }
            dds_slave_state_t st = {0};
            bool fault = false;
            const dds_slave_state_t *cached = dds_master_get_local_state(&s_dds, i);
            esp_err_t e;
            if (!fsm_commands_allowed() && dds_status_is_off(cached)) {
                st = *cached; fault = st.fault; e = ESP_OK; /* confirmed before rail-off */
            } else e = dds_master_get_status(&s_dds, i, &st, &fault);
            if (e == ESP_OK) {
                snprintf(resp, sizeof(resp),
                         "DDS S%d FREQ=%.3f WAVE=%s AMP=%.3f STATE=%s CURRENT=%.3f OUT=%d DUMMY=%d%s",
                         i, st.freq_hz, wnames[st.waveform], st.amplitude_mA,
                         dds_output_state_name(st.output_state), st.current_amplitude_uA / 1000.0f,
                         st.out_en, st.dummy_en, fault ? " FAULT" : "");
            } else {
                snprintf(resp, sizeof(resp), "DDS S%d ERR %s",
                         i, esp_err_to_name(e));
            }
            ble_notify_str(resp);
        }
        return;

    } else {
        ble_notify_str("DDS ERR CMD unknown");
        return;
    }

    if (err != ESP_OK) {
        /* Broadcast: errors already reported per-slave inside the loops. */
        if (!broadcast) {
            report_slave_fault(slave_idx, err);
        }
        return;
    }
    ble_notify_str(resp);
}

static void dds_cmd_task(void *param)
{
    dds_cmd_msg_t msg;
    for (;;) {
        fsm_service();
        if (xQueueReceive(s_dds_cmd_queue, &msg, pdMS_TO_TICKS(DDS_COMMAND_SPACING_MS)) == pdTRUE) {
            fsm_service();
            process_dds_command(msg.sub);
            vTaskDelay(pdMS_TO_TICKS(DDS_COMMAND_SPACING_MS));
        }
    }
}

static void handle_dds_command(const char *sub)
{
    dds_cmd_msg_t msg;
    strncpy(msg.sub, sub, DDS_CMD_MAX_LEN);
    msg.sub[DDS_CMD_MAX_LEN] = '\0';
    if (xQueueSend(s_dds_cmd_queue, &msg, 0) != pdTRUE) {
        ble_notify_str("DDS ERR busy — try again");
    }
}

/* ── GATT callbacks ─────────────────────────────────────────────────────── */

/* SMART PARSER FIX: Automatically splits concatenated/multi-line BLE strings safely */
static int nus_rx_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t data_len = OS_MBUF_PKTLEN(ctxt->om);
    char buf[256];
    if (data_len >= sizeof(buf)) {
        data_len = sizeof(buf) - 1;
    }
    ble_hs_mbuf_to_flat(ctxt->om, buf, data_len, NULL);
    buf[data_len] = '\0';

    ESP_LOGI(TAG, "RX <- \"%s\"", buf);

    /* Worker gates stimulation; STATUS/OFF remain available during faults. */

    /* Look for "DDS " anywhere in the string to support combined packets */
    char *ptr = strstr(buf, "DDS ");
    if (ptr != NULL) {
        while (ptr != NULL) {
            char *next = strstr(ptr + 4, "DDS ");
            char cmd_buf[256];
            
            if (next) {
                int len = next - ptr;
                /* Prevent off-by-one buffer overflow */
                if (len >= sizeof(cmd_buf)) {
                    len = sizeof(cmd_buf) - 1;
                }
                strncpy(cmd_buf, ptr, len);
                cmd_buf[len] = '\0';
            } else {
                strcpy(cmd_buf, ptr);
            }
            
            /* Strip trailing spaces, carriage returns, and newlines */
            int len = strlen(cmd_buf);
            while (len > 0 && (cmd_buf[len-1] == '\r' || cmd_buf[len-1] == '\n' || cmd_buf[len-1] == ' ')) {
                cmd_buf[len-1] = '\0';
                len--;
            }
            
            handle_dds_command(cmd_buf + 4); /* Skip the "DDS " prefix */
            ptr = next;
        }
    } else if (s_notify_enabled) {
        char reply[280];
        int  reply_len = snprintf(reply, sizeof(reply), "ECHO: %s", buf);
        struct os_mbuf *om = ble_hs_mbuf_from_flat(reply, reply_len);
        if (om) {
            ble_gatts_notify_custom(s_conn_handle, s_tx_char_handle, om);
            ESP_LOGI(TAG, "TX -> \"%s\"", reply);
        }
    }
    return 0;
}

static int nus_tx_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *msg = "ESP32-S3 NUS ready";
    return os_mbuf_append(ctxt->om, msg, strlen(msg));
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &NUS_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &NUS_TX_UUID.u,
                .access_cb  = nus_tx_read_cb,
                .val_handle = &s_tx_char_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid      = &NUS_RX_UUID.u,
                .access_cb = nus_rx_write_cb,
#if BLE_TEST_NO_SECURITY
                /* No encryption required — Chrome desktop testing mode */
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
#else
                /* WRITE_ENC: phone must be paired+encrypted before any
                 * DDS command is accepted. Unencrypted writes are rejected
                 * at the ATT layer — process_dds_command is never called.  */
                .flags     = BLE_GATT_CHR_F_WRITE     |
                             BLE_GATT_CHR_F_WRITE_NO_RSP |
                             BLE_GATT_CHR_F_WRITE_ENC,
#endif
            },
            { 0 },
        },
    },
    { 0 },
};

/* BLE connection intervals use 1.25 ms units; the supervision timeout uses
 * 10 ms units.  Keep the values together so the request and its log cannot
 * drift apart. */
#define BLE_CONN_ITVL_MIN            24U  /*  30 ms */
#define BLE_CONN_ITVL_MAX            80U  /* 100 ms */
#define BLE_CONN_LATENCY              0U
#define BLE_CONN_SUPERVISION_TIMEOUT 500U  /*   5 s */

static void ble_request_conn_params(uint16_t conn_handle)
{
    const struct ble_gap_upd_params params = {
        .itvl_min            = BLE_CONN_ITVL_MIN,
        .itvl_max            = BLE_CONN_ITVL_MAX,
        .latency             = BLE_CONN_LATENCY,
        .supervision_timeout = BLE_CONN_SUPERVISION_TIMEOUT,
        .min_ce_len          = BLE_GAP_INITIAL_CONN_MIN_CE_LEN,
        .max_ce_len          = BLE_GAP_INITIAL_CONN_MAX_CE_LEN,
    };

    int rc = ble_gap_update_params(conn_handle, &params);
    if (rc == BLE_HS_EALREADY) {
        /* The central may start its own update as soon as it connects.  Its
         * completion event gives us a safe point to retry our request. */
        s_conn_params_retry = true;
        ESP_LOGW(TAG, "Connection parameter update already active; deferring retry");
        return;
    }

    if (rc != 0) {
        s_conn_params_retry = false;
        ESP_LOGW(TAG, "Connection parameter request failed: rc=%d", rc);
        return;
    }

    s_conn_params_retry = false;
    ESP_LOGI(TAG,
             "Requested connection interval %u.%02u-%u.%02u ms, latency=%u, timeout=%u ms",
             (unsigned)(BLE_CONN_ITVL_MIN * 5U / 4U),
             (unsigned)((BLE_CONN_ITVL_MIN % 4U) * 25U),
             (unsigned)(BLE_CONN_ITVL_MAX * 5U / 4U),
             (unsigned)((BLE_CONN_ITVL_MAX % 4U) * 25U),
             (unsigned)BLE_CONN_LATENCY,
             (unsigned)(BLE_CONN_SUPERVISION_TIMEOUT * 10U));
}

/* ── GAP event handler ──────────────────────────────────────────────────── */
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle    = event->connect.conn_handle;
            s_notify_enabled = false;
            s_conn_params_retry = false;
            oled_set_ble_connected(true);
            ESP_LOGI(TAG, "Connected  handle=%d", s_conn_handle);

#if !BLE_TEST_NO_SECURITY
            /* Immediately initiate pairing/encryption — do not wait for
             * the central to request it. This ensures no DDS commands can
             * be accepted on an unencrypted link.                           */
            int rc = ble_gap_security_initiate(s_conn_handle);
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGE(TAG, "security_initiate failed: %d — disconnecting", rc);
                ble_gap_terminate(s_conn_handle, BLE_ERR_AUTH_FAIL);
            }
#else
            /* With no security procedure in progress, negotiate the relaxed
             * interval immediately. */
            ble_request_conn_params(s_conn_handle);
#endif
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_conn_params_retry = false;
            ble_advertise();
        }
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
#if !BLE_TEST_NO_SECURITY
        /* Encryption handshake complete (or failed). Reject unencrypted links. */
        if (event->enc_change.status != 0) {
            ESP_LOGW(TAG, "Encryption failed (status=%d) — disconnecting",
                     event->enc_change.status);
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_AUTH_FAIL);
        } else {
            ESP_LOGI(TAG, "Link encrypted successfully");
            /* Start this only after pairing so the two link-layer procedures
             * cannot contend with each other. */
            ble_request_conn_params(event->enc_change.conn_handle);
        }
#endif
        break;

    case BLE_GAP_EVENT_CONN_UPDATE: {
        if (event->conn_update.status != 0) {
            ESP_LOGW(TAG, "Connection parameter update rejected: status=%d",
                     event->conn_update.status);
        } else {
            struct ble_gap_conn_desc desc;
            int rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
            if (rc != 0) {
                ESP_LOGW(TAG, "Cannot read updated connection parameters: rc=%d", rc);
            } else {
                ESP_LOGI(TAG,
                         "Connection updated: interval=%u.%02u ms, latency=%u, timeout=%u ms",
                         (unsigned)(desc.conn_itvl * 5U / 4U),
                         (unsigned)((desc.conn_itvl % 4U) * 25U),
                         (unsigned)desc.conn_latency,
                         (unsigned)(desc.supervision_timeout * 10U));
            }
        }

        if (s_conn_params_retry &&
            event->conn_update.conn_handle == s_conn_handle) {
            ESP_LOGI(TAG, "Retrying deferred connection parameter request");
            ble_request_conn_params(s_conn_handle);
        }
        break;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        fsm_handle_event(EV_BLE_DISCONNECT);
        s_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        s_conn_params_retry = false;
        oled_set_ble_connected(false);
        ble_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_char_handle) {
            s_notify_enabled = event->subscribe.cur_notify;
            /* Deliver any boot-time hardware fault now that we have a listener. */
            if (s_notify_enabled && s_boot_fault_msg[0] != '\0') {
                ble_notify_str(s_boot_fault_msg);
            }
        }
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* NimBLE asks us to display or confirm a passkey during pairing.
         * DISP_ONLY: generate a random 6-digit key, log it to UART so the
         * user can type it into the Windows Bluetooth pairing dialog.       */
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io io = {0};
            io.action   = BLE_SM_IOACT_DISP;
            io.passkey  = 100000 + (esp_random() % 900000);
            ESP_LOGI(TAG, ">>>>>> BLE PASSKEY: %06lu <<<<<<",
                     (unsigned long)io.passkey);
            /* TODO: show io.passkey on your OLED here */
            int rc = ble_sm_inject_io(event->passkey.conn_handle, &io);
            if (rc != 0) {
                ESP_LOGE(TAG, "ble_sm_inject_io failed: %d", rc);
            }
        }
        break;

    default:
        break;
    }
    return 0;
}

static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl            = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name                  = (const uint8_t *)DEVICE_NAME;
    fields.name_len              = strlen(DEVICE_NAME);
    fields.name_is_complete      = 1;
    fields.uuids128              = &NUS_SERVICE_UUID;
    fields.num_uuids128          = 1;
    fields.uuids128_is_complete  = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    /* Slow advertising interval: ~1 s (1600 × 0.625 ms).
     * Reduces radio duty cycle significantly while idle.
     * Latency to discovery is ~1 s — acceptable for this application. */
    adv_params.itvl_min  = 1600;   /* 1000 ms */
    adv_params.itvl_max  = 1600;   /* 1000 ms */

    ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                      &adv_params, gap_event_cb, NULL);
}

static void ble_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    ble_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset  reason=%d", reason);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void heartbeat_task(void *param)
{
    uint32_t seq = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_notify_enabled) continue;

        char msg[64];
        int  len = snprintf(msg, sizeof(msg), "HB#%lu uptime=%lus",
                            (unsigned long)seq,
                            (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000UL));
        struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, len);
        if (om) ble_gatts_notify_custom(s_conn_handle, s_tx_char_handle, om);
        seq++;
    }
}

/* ── Soft-stop button task ──────────────────────────────────────────────── */
/* GPIO ISR wakes the task on falling edge (PIN_SOFT_STOP_PB, active-low).
 * The task then reads the level, debounces, and drives the FSM.
 * Blocks indefinitely between edges — zero CPU overhead while idle. */
static TaskHandle_t s_soft_stop_task_handle;

static void IRAM_ATTR soft_stop_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_soft_stop_task_handle, &woken);
    portYIELD_FROM_ISR(woken);
}

static void soft_stop_task(void *param)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_SOFT_STOP_PB,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,   /* external pull-up on the PCB */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,     /* press only — active-low */
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    /* Keep the active-mode GPIO config (NEGEDGE interrupt + pull-up) during
     * BLE light-sleep.  Without this the GPIO matrix swaps GPIO 9 to its
     * default sleep config (interrupts disabled) and the edge is never seen. */
    gpio_sleep_sel_dis(PIN_SOFT_STOP_PB);
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_SOFT_STOP_PB, soft_stop_isr, NULL));
    ESP_LOGI("SOFT_STOP", "ISR registered on GPIO %d", PIN_SOFT_STOP_PB);

    for (;;) {
        /* 1. Sleep indefinitely until the ISR fires the first time */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 

        /* 2. Wait just long enough for the physical button to stop bouncing */
        vTaskDelay(pdMS_TO_TICKS(50)); 

        /* 3. Read the stable level */
        int level = gpio_get_level(PIN_SOFT_STOP_PB);
        ESP_LOGI("SOFT_STOP", "Task triggered — GPIO %d level=%d", PIN_SOFT_STOP_PB, level);

        if (level == 0) {   /* still pressed */
            fsm_handle_event(EV_EMERGENCY);
            ble_notify_str("SOFT STOP - ramp-down requested");
        }

        /* 4. Flush any extra ISR triggers that queued up 
         * while the task was busy delaying or running the FSM event. */
        ulTaskNotifyTake(pdTRUE, 0); 
    }
}

/* ── app_main ───────────────────────────────────────────────────────────── */
void app_main(void)
{
    /* Disable all HV channel rails immediately — before any other init. */
    ESP_ERROR_CHECK(channel_rails_init());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── Power management: automatic light-sleep ────────────────────────────
     * ESP-IDF's tickless idle will enter light-sleep whenever all FreeRTOS
     * tasks are blocked.  NimBLE schedules its own radio wakeups, so BLE
     * connectivity is preserved.  The soft-stop GPIO ISR is already protected
     * with gpio_sleep_sel_dis(), so that edge is never missed.
     *
     * max_freq / min_freq: S3 can scale between 10–240 MHz.  Set both to 240
     * to avoid dynamic scaling side-effects on I2C timing (the I2C peripheral
     * clock is derived from APB, which tracks CPU freq on S3).
     * light_sleep_enable: true — the key flag that activates this feature.   */
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz       = 160,  /* must match CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ */
        .min_freq_mhz       = 160,  /* fixed to avoid I2C APB clock scaling issues */
        .light_sleep_enable = true,
    };
    esp_err_t pm_err = esp_pm_configure(&pm_cfg);
    if (pm_err != ESP_OK) {
        ESP_LOGW(TAG, "esp_pm_configure failed (%s) — running without sleep",
                 esp_err_to_name(pm_err));
    } else {
        ESP_LOGI(TAG, "Automatic light-sleep enabled");
    }

    fsm_init(&s_dds);   /* starts in STATE_BOOT */

    /* Enable LV rails before DDS bus init so the slaves power up with their
     * supply rails already stable.  Rails were driven LOW in channel_rails_init()
     * at the very top of app_main(), so there is no premature HV exposure.   */
    for (uint8_t ch = 0; ch < DDS_NUM_SLAVES; ch++) {
        ESP_ERROR_CHECK(channel_rails_set(ch, true));
    }
    /* LV boot only: HV remains off until validated card-ready status. */
    vTaskDelay(pdMS_TO_TICKS(DDS_LV_BOOT_MS));

    /*Enable the DDS bus*/
    esp_err_t dds_err = dds_master_init_new_bus(&s_dds,
                                            PIN_DDS_SDA, PIN_DDS_SCL,
                                            I2C_NUM_0, DDS_I2C_SPEED_HZ);
    if (dds_err != ESP_OK) {
        ESP_LOGE(TAG, "DDS master init failed: %s", esp_err_to_name(dds_err));
        snprintf(s_boot_fault_msg, sizeof(s_boot_fault_msg),
                 "HW ERR DDS init failed: %s", esp_err_to_name(dds_err));
        channel_rails_disable_all(); /* No output authorized at boot. */
        fsm_handle_event(EV_HW_FAIL);
        fsm_service();
    } else {
        /* Validate v2 card status before any HV rail is enabled. */
        fsm_power_up(); /* card ready -> HV on -> settle -> available */
    }

    /*Enable the OLED display*/
    if (oled_init()) {
        oled_start_display_task(&s_dds);
    }

    s_dds_cmd_queue = xQueueCreate(DDS_CMD_QUEUE_LEN, sizeof(dds_cmd_msg_t));
    configASSERT(s_dds_cmd_queue);
    BaseType_t worker_created = xTaskCreate(dds_cmd_task, "dds_cmd", 4096, NULL, 5, NULL);
    configASSERT(worker_created == pdPASS);

    nimble_port_init();
    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* ── Security configuration ─────────────────────────────────────────
     * Set BLE_TEST_NO_SECURITY 1 for Chrome desktop testing (no pairing).
     * Set BLE_TEST_NO_SECURITY 0 for Android production (full security).   */
#if BLE_TEST_NO_SECURITY
    ble_hs_cfg.sm_io_cap  = BLE_SM_IO_CAP_NO_IO;  /* "Just Works" — no passkey */
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm    = 0;
    ble_hs_cfg.sm_sc      = 0;
    ESP_LOGW(TAG, "*** BLE SECURITY DISABLED — testing mode ***");
#else
    /* sm_io_cap    DISP_ONLY: ESP32 displays a passkey; user confirms on phone.
     * sm_bonding   Store the Long-Term Key in NVS so re-pairing is not needed
     *              on every reconnect.
     * sm_mitm      Require man-in-the-middle protection (passkey comparison).
     *              Without this, an attacker within BLE range could intercept
     *              the pairing and inject DDS commands to the high-power output.
     * sm_sc        BLE Secure Connections (ECDH key exchange, BLE 4.2+).
     *              Significantly stronger than legacy pairing.
     * key_dist     Exchange encryption + identity keys in both directions so
     *              bonding works correctly after reconnect.                  */
    ble_hs_cfg.sm_io_cap         = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding        = 1;
    ble_hs_cfg.sm_mitm           = 1;
    ble_hs_cfg.sm_sc             = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
#endif

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);
    ble_svc_gap_device_name_set(DEVICE_NAME);

    gpio_install_isr_service(0);

    nimble_port_freertos_init(nimble_host_task);
    xTaskCreate(heartbeat_task,  "ble_heartbeat", 4096, NULL, 5, NULL);
    xTaskCreate(soft_stop_task,  "soft_stop",     4096, NULL, 6,
                &s_soft_stop_task_handle);
}
