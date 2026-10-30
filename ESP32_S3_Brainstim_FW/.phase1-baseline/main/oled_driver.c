#include "oled_driver.h"
#include "i2c_dds_master.h"
#include "battery_monitor.h"
#include "fsm.h"

/* Set to 1 when testing WITHOUT a battery connected.
 * ADC is not read without a battery but OLED will still show a battery icon.
 * Set to 0 for normal operation. */
#define NO_BATTERY_TEST_MODE   0

#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "OLED";

/* ---------------------------------------------------------------------------
 * SSD1306 control byte constants (I2C protocol)
 *   Byte layout:  [Co | D/C# | 0 0 0 0 0 0]
 *   Co   = 0 → no more control bytes; all following bytes are data/cmds
 *   D/C# = 0 → command stream
 *   D/C# = 1 → GDDRAM data stream
 * ------------------------------------------------------------------------- */
#define SSD1306_CTRL_CMD        0x00    /* Co=0, D/C=0 — command bytes follow  */
#define SSD1306_CTRL_DATA       0x40    /* Co=0, D/C=1 — GDDRAM data follows   */

/* ---------------------------------------------------------------------------
 * SSD1306 command bytes (commonly used subset)
 * ------------------------------------------------------------------------- */
#define SSD1306_CMD_DISPLAY_OFF         0xAE
#define SSD1306_CMD_DISPLAY_ON          0xAF
#define SSD1306_CMD_SET_CLK_DIV         0xD5
#define SSD1306_CMD_SET_MUX             0xA8
#define SSD1306_CMD_SET_DISP_OFFSET     0xD3
#define SSD1306_CMD_SET_START_LINE      0x40
#define SSD1306_CMD_CHARGE_PUMP         0x8D
#define SSD1306_CMD_MEM_ADDR_MODE       0x20
#define SSD1306_CMD_SEG_REMAP           0xA1    /* col 127 → SEG0 (flip H) */
#define SSD1306_CMD_COM_SCAN_DEC        0xC8    /* flip vertical            */
#define SSD1306_CMD_SET_COM_PINS        0xDA
#define SSD1306_CMD_SET_CONTRAST        0x81
#define SSD1306_CMD_SET_PRECHARGE       0xD9
#define SSD1306_CMD_SET_VCOMH           0xDB
#define SSD1306_CMD_RAM_ON              0xA4    /* display follows RAM      */
#define SSD1306_CMD_NORMAL_DISP         0xA6
#define SSD1306_CMD_SET_COL_ADDR        0x21
#define SSD1306_CMD_SET_PAGE_ADDR       0x22

/* ---------------------------------------------------------------------------
 * Driver state
 * ------------------------------------------------------------------------- */
static i2c_master_bus_handle_t  s_i2c_bus     = NULL;  /* own I2C_NUM_1 bus */
static i2c_master_dev_handle_t  s_oled_dev    = NULL;
static bool                     s_initialized = false;

/* 128 × 8-page framebuffer — each byte is one 8-pixel-tall column */
static uint8_t s_framebuf[OLED_PAGES][OLED_WIDTH];

/* ---------------------------------------------------------------------------
 * Standard 5 × 8 bitmap font — printable ASCII 0x20 (' ') … 0x7F
 *
 * Each entry is 5 bytes representing 5 columns of the glyph.
 * Bit 0 of each byte = topmost pixel row; bit 7 = bottommost.
 * An implicit 6th column of 0x00 (inter-character gap) is added in software.
 * ------------------------------------------------------------------------- */
static const uint8_t s_font5x8[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},  /* 0x20  ' '  */
    {0x00,0x00,0x5F,0x00,0x00},  /* 0x21  '!'  */
    {0x00,0x07,0x00,0x07,0x00},  /* 0x22  '"'  */
    {0x14,0x7F,0x14,0x7F,0x14},  /* 0x23  '#'  */
    {0x24,0x2A,0x7F,0x2A,0x12},  /* 0x24  '$'  */
    {0x23,0x13,0x08,0x64,0x62},  /* 0x25  '%'  */
    {0x36,0x49,0x55,0x22,0x50},  /* 0x26  '&'  */
    {0x00,0x05,0x03,0x00,0x00},  /* 0x27  '\'' */
    {0x00,0x1C,0x22,0x41,0x00},  /* 0x28  '('  */
    {0x00,0x41,0x22,0x1C,0x00},  /* 0x29  ')'  */
    {0x14,0x08,0x3E,0x08,0x14},  /* 0x2A  '*'  */
    {0x08,0x08,0x3E,0x08,0x08},  /* 0x2B  '+'  */
    {0x00,0x50,0x30,0x00,0x00},  /* 0x2C  ','  */
    {0x08,0x08,0x08,0x08,0x08},  /* 0x2D  '-'  */
    {0x00,0x60,0x60,0x00,0x00},  /* 0x2E  '.'  */
    {0x20,0x10,0x08,0x04,0x02},  /* 0x2F  '/'  */
    {0x3E,0x51,0x49,0x45,0x3E},  /* 0x30  '0'  */
    {0x00,0x42,0x7F,0x40,0x00},  /* 0x31  '1'  */
    {0x42,0x61,0x51,0x49,0x46},  /* 0x32  '2'  */
    {0x21,0x41,0x45,0x4B,0x31},  /* 0x33  '3'  */
    {0x18,0x14,0x12,0x7F,0x10},  /* 0x34  '4'  */
    {0x27,0x45,0x45,0x45,0x39},  /* 0x35  '5'  */
    {0x3C,0x4A,0x49,0x49,0x30},  /* 0x36  '6'  */
    {0x01,0x71,0x09,0x05,0x03},  /* 0x37  '7'  */
    {0x36,0x49,0x49,0x49,0x36},  /* 0x38  '8'  */
    {0x06,0x49,0x49,0x29,0x1E},  /* 0x39  '9'  */
    {0x00,0x36,0x36,0x00,0x00},  /* 0x3A  ':'  */
    {0x00,0x56,0x36,0x00,0x00},  /* 0x3B  ';'  */
    {0x08,0x14,0x22,0x41,0x00},  /* 0x3C  '<'  */
    {0x14,0x14,0x14,0x14,0x14},  /* 0x3D  '='  */
    {0x00,0x41,0x22,0x14,0x08},  /* 0x3E  '>'  */
    {0x02,0x01,0x51,0x09,0x06},  /* 0x3F  '?'  */
    {0x32,0x49,0x79,0x41,0x3E},  /* 0x40  '@'  */
    {0x7E,0x11,0x11,0x11,0x7E},  /* 0x41  'A'  */
    {0x7F,0x49,0x49,0x49,0x36},  /* 0x42  'B'  */
    {0x3E,0x41,0x41,0x41,0x22},  /* 0x43  'C'  */
    {0x7F,0x41,0x41,0x22,0x1C},  /* 0x44  'D'  */
    {0x7F,0x49,0x49,0x49,0x41},  /* 0x45  'E'  */
    {0x7F,0x09,0x09,0x09,0x01},  /* 0x46  'F'  */
    {0x3E,0x41,0x49,0x49,0x7A},  /* 0x47  'G'  */
    {0x7F,0x08,0x08,0x08,0x7F},  /* 0x48  'H'  */
    {0x00,0x41,0x7F,0x41,0x00},  /* 0x49  'I'  */
    {0x20,0x40,0x41,0x3F,0x01},  /* 0x4A  'J'  */
    {0x7F,0x08,0x14,0x22,0x41},  /* 0x4B  'K'  */
    {0x7F,0x40,0x40,0x40,0x40},  /* 0x4C  'L'  */
    {0x7F,0x02,0x04,0x02,0x7F},  /* 0x4D  'M'  */
    {0x7F,0x04,0x08,0x10,0x7F},  /* 0x4E  'N'  */
    {0x3E,0x41,0x41,0x41,0x3E},  /* 0x4F  'O'  */
    {0x7F,0x09,0x09,0x09,0x06},  /* 0x50  'P'  */
    {0x3E,0x41,0x51,0x21,0x5E},  /* 0x51  'Q'  */
    {0x7F,0x09,0x19,0x29,0x46},  /* 0x52  'R'  */
    {0x46,0x49,0x49,0x49,0x31},  /* 0x53  'S'  */
    {0x01,0x01,0x7F,0x01,0x01},  /* 0x54  'T'  */
    {0x3F,0x40,0x40,0x40,0x3F},  /* 0x55  'U'  */
    {0x1F,0x20,0x40,0x20,0x1F},  /* 0x56  'V'  */
    {0x3F,0x40,0x38,0x40,0x3F},  /* 0x57  'W'  */
    {0x63,0x14,0x08,0x14,0x63},  /* 0x58  'X'  */
    {0x07,0x08,0x70,0x08,0x07},  /* 0x59  'Y'  */
    {0x61,0x51,0x49,0x45,0x43},  /* 0x5A  'Z'  */
    {0x00,0x7F,0x41,0x41,0x00},  /* 0x5B  '['  */
    {0x02,0x04,0x08,0x10,0x20},  /* 0x5C  '\\' */
    {0x00,0x41,0x41,0x7F,0x00},  /* 0x5D  ']'  */
    {0x04,0x02,0x01,0x02,0x04},  /* 0x5E  '^'  */
    {0x40,0x40,0x40,0x40,0x40},  /* 0x5F  '_'  */
    {0x00,0x01,0x02,0x04,0x00},  /* 0x60  '`'  */
    {0x20,0x54,0x54,0x54,0x78},  /* 0x61  'a'  */
    {0x7F,0x48,0x44,0x44,0x38},  /* 0x62  'b'  */
    {0x38,0x44,0x44,0x44,0x20},  /* 0x63  'c'  */
    {0x38,0x44,0x44,0x48,0x7F},  /* 0x64  'd'  */
    {0x38,0x54,0x54,0x54,0x18},  /* 0x65  'e'  */
    {0x08,0x7E,0x09,0x01,0x02},  /* 0x66  'f'  */
    {0x0C,0x52,0x52,0x52,0x3E},  /* 0x67  'g'  */
    {0x7F,0x08,0x04,0x04,0x78},  /* 0x68  'h'  */
    {0x00,0x44,0x7D,0x40,0x00},  /* 0x69  'i'  */
    {0x20,0x40,0x44,0x3D,0x00},  /* 0x6A  'j'  */
    {0x7F,0x10,0x28,0x44,0x00},  /* 0x6B  'k'  */
    {0x00,0x41,0x7F,0x40,0x00},  /* 0x6C  'l'  */
    {0x7C,0x04,0x18,0x04,0x78},  /* 0x6D  'm'  */
    {0x7C,0x08,0x04,0x04,0x78},  /* 0x6E  'n'  */
    {0x38,0x44,0x44,0x44,0x38},  /* 0x6F  'o'  */
    {0x7C,0x14,0x14,0x14,0x08},  /* 0x70  'p'  */
    {0x08,0x14,0x14,0x18,0x7C},  /* 0x71  'q'  */
    {0x7C,0x08,0x04,0x04,0x08},  /* 0x72  'r'  */
    {0x48,0x54,0x54,0x54,0x20},  /* 0x73  's'  */
    {0x04,0x3F,0x44,0x40,0x20},  /* 0x74  't'  */
    {0x3C,0x40,0x40,0x20,0x7C},  /* 0x75  'u'  */
    {0x1C,0x20,0x40,0x20,0x1C},  /* 0x76  'v'  */
    {0x3C,0x40,0x30,0x40,0x3C},  /* 0x77  'w'  */
    {0x44,0x28,0x10,0x28,0x44},  /* 0x78  'x'  */
    {0x0C,0x50,0x50,0x50,0x3C},  /* 0x79  'y'  */
    {0x44,0x64,0x54,0x4C,0x44},  /* 0x7A  'z'  */
    {0x00,0x08,0x36,0x41,0x00},  /* 0x7B  '{'  */
    {0x00,0x00,0x7F,0x00,0x00},  /* 0x7C  '|'  */
    {0x00,0x41,0x36,0x08,0x00},  /* 0x7D  '}'  */
    {0x10,0x08,0x08,0x10,0x08},  /* 0x7E  '~'  */
    {0x7E,0x55,0x55,0x41,0x7E},  /* 0x7F  DEL  */
};

/* ===========================================================================
 * Low-level I2C helpers
 * =========================================================================*/

static esp_err_t oled_send_cmds(const uint8_t *cmds, size_t len)
{
    /* I2C frame: [0x00, cmd0, cmd1, ...] */
    uint8_t buf[64];
    if (len + 1 > sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    buf[0] = SSD1306_CTRL_CMD;
    memcpy(buf + 1, cmds, len);
    return i2c_master_transmit(s_oled_dev, buf, len + 1, OLED_I2C_TIMEOUT_MS);
}

static esp_err_t oled_send_data(const uint8_t *data, size_t len)
{
    /* I2C frame: [0x40, d0, d1, ...] — maximum one page (128 B) at a time  */
    uint8_t buf[OLED_WIDTH + 1];
    if (len > OLED_WIDTH) len = OLED_WIDTH;
    buf[0] = SSD1306_CTRL_DATA;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(s_oled_dev, buf, len + 1, OLED_I2C_TIMEOUT_MS);
}

/* Set the SSD1306 write-pointer to (page, col) using horizontal addressing */
static void oled_set_pos(uint8_t page, uint8_t col)
{
    uint8_t cmds[] = {
        SSD1306_CMD_SET_COL_ADDR,  col,        OLED_WIDTH - 1,
        SSD1306_CMD_SET_PAGE_ADDR, page, OLED_PAGES - 1,
    };
    oled_send_cmds(cmds, sizeof(cmds));
}

/* ===========================================================================
 * Public API
 * =========================================================================*/

bool oled_init(void)
{
    /* Create a dedicated I2C_NUM_1 bus — fully independent of the DDS master bus */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = I2C_NUM_1,
        .sda_io_num          = OLED_I2C_SDA_PIN,   /* GPIO 16 */
        .scl_io_num          = OLED_I2C_SCL_PIN,   /* GPIO 17 */
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus (I2C_NUM_1) failed: %s", esp_err_to_name(ret));
        return false;
    }

    /* Register the OLED device on the new bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OLED_I2C_ADDR,
        .scl_speed_hz    = OLED_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_oled_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return false;
    }

    /* SSD1306 power-on initialisation sequence for 128×64 */
    static const uint8_t init_cmds[] = {
        SSD1306_CMD_DISPLAY_OFF,
        SSD1306_CMD_SET_CLK_DIV,    0x80,   /* divide ratio=1, osc=8        */
        SSD1306_CMD_SET_MUX,        0x3F,   /* 64-row multiplex             */
        SSD1306_CMD_SET_DISP_OFFSET,0x00,   /* no vertical shift            */
        SSD1306_CMD_SET_START_LINE,         /* start line = 0               */
        SSD1306_CMD_CHARGE_PUMP,    0x14,   /* internal charge pump ON      */
        SSD1306_CMD_MEM_ADDR_MODE,  0x00,   /* horizontal addressing mode   */
        SSD1306_CMD_SEG_REMAP,              /* col 127 → SEG0 (flip H)      */
        SSD1306_CMD_COM_SCAN_DEC,           /* flip vertical                */
        SSD1306_CMD_SET_COM_PINS,   0x12,   /* alt COM config               */
        SSD1306_CMD_SET_CONTRAST,   0xCF,   /* contrast ~81 %               */
        SSD1306_CMD_SET_PRECHARGE,  0xF1,   /* phase1=1, phase2=15          */
        SSD1306_CMD_SET_VCOMH,      0x40,   /* VCOMH = 0.77×VCC             */
        SSD1306_CMD_RAM_ON,                 /* display follows GDDRAM       */
        SSD1306_CMD_NORMAL_DISP,            /* not inverted                 */
        SSD1306_CMD_DISPLAY_ON,
    };

    ret = oled_send_cmds(init_cmds, sizeof(init_cmds));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init sequence failed: %s", esp_err_to_name(ret));
        return false;
    }

    memset(s_framebuf, 0, sizeof(s_framebuf));
    oled_flush();   /* blank the display */

    s_initialized = true;
    ESP_LOGI(TAG, "SSD1306 OLED ready (128x64 @ 0x%02X, I2C1 SDA=%d SCL=%d)",
             OLED_I2C_ADDR, OLED_I2C_SDA_PIN, OLED_I2C_SCL_PIN);
    return true;
}

/* ---------------------------------------------------------------------------
 * oled_write_string — render string into framebuffer at (page, col)
 * ------------------------------------------------------------------------- */
void oled_write_string(uint8_t page, uint8_t col, const char *str)
{
    if (!s_initialized || !str) return;
    if (page >= OLED_PAGES) return;

    while (*str && col < OLED_WIDTH) {
        uint8_t c = (uint8_t)*str++;
        if (c < 0x20 || c > 0x7F) c = 0x20;   /* replace non-printable    */

        const uint8_t *glyph = s_font5x8[c - 0x20];
        for (int i = 0; i < 5 && col < OLED_WIDTH; i++, col++) {
            s_framebuf[page][col] = glyph[i];
        }
        /* inter-character gap */
        if (col < OLED_WIDTH) {
            s_framebuf[page][col++] = 0x00;
        }
    }
}

/* ---------------------------------------------------------------------------
 * oled_clear — zero framebuffer + blank display
 * ------------------------------------------------------------------------- */
void oled_clear(void)
{
    if (!s_initialized) return;
    memset(s_framebuf, 0, sizeof(s_framebuf));
    oled_flush();
}

/* ---------------------------------------------------------------------------
 * oled_flush — push framebuffer to display one page at a time.
 *
 * Sending one page (128 B) per I2C transaction with a brief yield between
 * pages allows other tasks to remain responsive during the update.
 * ------------------------------------------------------------------------- */
void oled_flush(void)
{
    if (!s_initialized) return;

    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        oled_set_pos(page, 0);
        oled_send_data(s_framebuf[page], OLED_WIDTH);
        vTaskDelay(1);
    }
}

/* ===========================================================================
 * Slave status display
 * =========================================================================*/

/* 3-character waveform abbreviations for compact single-row display */
static const char *s_wave_abbr[4] = { "SIN", "SQR", "TRI", "SAW" };

/* ---------------------------------------------------------------------------
 * Battery indicator state — 0-100 = charge %, 255 = unknown
 * ------------------------------------------------------------------------- */
static uint8_t s_battery_pct    = 255;   /* unknown until oled_set_battery_level() */
static bool    s_ble_connected  = false; /* updated by oled_set_ble_connected()    */

void oled_set_battery_level(uint8_t percent)
{
    s_battery_pct = (percent > 100 && percent != 255) ? 100 : percent;
}

void oled_set_ble_connected(bool connected)
{
    s_ble_connected = connected;
}

/* ---------------------------------------------------------------------------
 * oled_draw_bt_icon — render a 5×8 Bluetooth symbol when BLE is connected.
 *
 * Position: page 0, columns 108-112 (5 px glyph, col 113 = gap).
 *
 * Column-encoded bitmap (bit 0 = top row, bit 7 = bottom row):
 *
 *   col 108: 0x42  rows 1,6  (outer diagonals — top & bottom)
 *   col 109: 0x24  rows 2,5  (inner diagonals)
 *   col 110: 0x7E  rows 1-6  (vertical spine)
 *   col 111: 0x24  rows 2,5  (inner diagonals)
 *   col 112: 0x42  rows 1,6  (outer diagonals — top & bottom)
 *
 * Renders as (. = off, * = on):
 *   row 0: . . . . .
 *   row 1: * . * . *
 *   row 2: . * * * .
 *   row 3: . . * . .
 *   row 4: . . * . .
 *   row 5: . * * * .
 *   row 6: * . * . *
 *   row 7: . . . . .
 * ------------------------------------------------------------------------- */
static const uint8_t s_bt_icon[5] = { 0x42, 0x24, 0x7E, 0x24, 0x42 };

static void oled_draw_bt_icon(void)
{
    if (!s_ble_connected) return;
    for (uint8_t i = 0; i < 5; i++) {
        s_framebuf[0][108 + i] = s_bt_icon[i];
    }
    /* col 113 stays 0x00 (gap — cleared by memset in oled_update_slave_display) */
}

/* ---------------------------------------------------------------------------
 * oled_draw_battery — render battery icon into the framebuffer.
 *
 * Position: page 0, columns 114-127 (shifted right 6 px vs original to
 * leave room for the Bluetooth icon at cols 108-113).
 *
 * Icon layout (8 px tall × 14 px wide):
 *
 *   cols 114-125  battery body (12 px wide)
 *     col 114     left wall      0xFF = ████████
 *     col 115-124 interior fill  0xFF = filled  |  0x81 = top+bot outline
 *     col 125     right wall     0xFF = ████████
 *   cols 126-127  terminal nub   0x3C = ..████.. (middle 4 px)
 *
 * Fill grows left-to-right proportional to s_battery_pct.
 * When s_battery_pct == 255, alternating columns signal "unknown".
 * ------------------------------------------------------------------------- */
static void oled_draw_battery(void)
{
    const uint8_t COL_LEFT  = 114;   /* left wall (was 108, shifted for BT icon) */
    const uint8_t COL_RIGHT = 125;   /* right wall                               */
    const uint8_t INT_COLS  = COL_RIGHT - COL_LEFT - 1;  /* 10 interior cols     */

    s_framebuf[0][COL_LEFT]  = 0xFF; /* left wall  */
    s_framebuf[0][COL_RIGHT] = 0xFF; /* right wall */
    s_framebuf[0][126]       = 0x3C; /* nub: bits 2-5 = middle 4 px              */
    s_framebuf[0][127]       = 0x3C;

    if (s_battery_pct == 255) {
        /* Unknown — alternating filled/outline columns */
        for (uint8_t i = 0; i < INT_COLS; i++) {
            s_framebuf[0][COL_LEFT + 1 + i] = (i & 1) ? 0xFF : 0x81;
        }
        return;
    }

    uint8_t fill = (uint8_t)(((uint16_t)INT_COLS * s_battery_pct + 50) / 100);
    for (uint8_t i = 0; i < INT_COLS; i++) {
        s_framebuf[0][COL_LEFT + 1 + i] = (i < fill) ? 0xFF : 0x81;
    }
}

/* ---------------------------------------------------------------------------
 * oled_update_slave_display
 *
 * Layout (21 chars × 8 rows, 1 row per DDS slave):
 *
 *   Row 0: "S0 SIN   440Hz ON "
 *   Row 1: "S1 SQR  1000Hz OFF"
 *   Row 2: "S2 TRI   200Hz ON "
 *   Row 3: "S3 SAW   100Hz OFF"
 *   Row 4: "S4 SIN   880Hz ON "
 *   Row 5: "S5 SQR   500Hz ON "
 *   Row 6: "S6 TRI    50Hz OFF"
 *   Row 7: "S7 SAW  2000Hz ON "
 *
 * State is read from the DDS master's local cache (updated on every
 * successful write command). No I2C transactions are performed here.
 * ------------------------------------------------------------------------- */
void oled_update_slave_display(const dds_master_t *dds)
{
    if (!s_initialized || !dds) return;

    /* Emergency state: replace slave display with full-screen warning */
    if (fsm_get_state() == STATE_EMERGENCY) {
        memset(s_framebuf, 0, sizeof(s_framebuf));
        oled_write_string(2, 19, "!! EMERGENCY !!");
        oled_write_string(4, 22, "RESET REQUIRED");
        oled_draw_bt_icon();
        oled_draw_battery();
        oled_flush();
        return;
    }

    /* Fault state: replace slave display with full-screen warning */
    if (fsm_get_state() == STATE_FAULT) {
        memset(s_framebuf, 0, sizeof(s_framebuf));
        oled_write_string(2, 19, "!! HW FAULT !!");
        oled_write_string(4, 22, "RESET REQUIRED");
        oled_draw_bt_icon();
        oled_draw_battery();
        oled_flush();
        return;
    }

    memset(s_framebuf, 0, sizeof(s_framebuf));

    for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
        char line[OLED_CHARS_PER_ROW + 1];
        const dds_slave_state_t *st = dds_master_get_local_state(dds, i);

        if (st) {
            /* "S%d %-3s %5.0fHz %-3s"  →  18 chars, safely within 21 */
            snprintf(line, sizeof(line), "S%d %-3s %5.0fHz %-3s",
                     i,
                     s_wave_abbr[st->waveform],
                     st->freq_hz,
                     st->enabled ? "ON" : "OFF");
        } else {
            snprintf(line, sizeof(line), "S%d ---", i);
        }

        oled_write_string(i, 0, line);
    }

    oled_draw_bt_icon();  /* overlay BT icon (cols 108-112) when connected  */
    oled_draw_battery();  /* overlay battery icon (cols 114-127)            */
    oled_flush();
}

/* ===========================================================================
 * FreeRTOS display task
 * =========================================================================*/

static void oled_display_task(void *pvParameters)
{
    const dds_master_t *dds = (const dds_master_t *)pvParameters;
    ESP_LOGI(TAG, "Display task started — refresh every %d ms", OLED_UPDATE_MS);

#if !NO_BATTERY_TEST_MODE
    bool battery_ok = battery_monitor_init();
    if (!battery_ok) {
        ESP_LOGE(TAG, "Battery monitor init failed — battery sampling disabled");
    }
#endif

    for (;;) {
#if !NO_BATTERY_TEST_MODE
        float voltage_v = 0.0f;
        if (battery_ok && battery_monitor_sample(&voltage_v)) {
            int pct = battery_monitor_estimate_percentage(voltage_v);
            oled_set_battery_level((uint8_t)pct);
            ESP_LOGD(TAG, "Battery: %.2fV (%d%%)", voltage_v, pct);

            if (voltage_v <= battery_monitor_crit_threshold()) {
                ESP_LOGE(TAG, "Battery critical (%.2fV <= %.2fV) — HW fault",
                         voltage_v, battery_monitor_crit_threshold());
                fsm_handle_event(EV_BATT_CRIT);
            }
        }
#endif

        oled_update_slave_display(dds);
        vTaskDelay(pdMS_TO_TICKS(OLED_UPDATE_MS));
    }
}

void oled_start_display_task(const dds_master_t *dds)
{
    xTaskCreate(oled_display_task, "oled_display", 4096, (void *)dds, 3, NULL);
}
