#ifndef GPIO_PINS_H
#define GPIO_PINS_H

/* Phase 1 operating limits and timing. Keep board-specific values here. */
#define DDS_NUM_SLAVES              4
#define DDS_MIN_FREQ_HZ             500.0f
#define DDS_MAX_FREQ_HZ             30000.0f
#define DDS_MAX_AMPLITUDE_UA        5000U
#define DDS_MAX_AMPLITUDE_MA        (DDS_MAX_AMPLITUDE_UA / 1000.0f)
#define DDS_SLAVE_BASE_ADDR         0x50
#define DDS_I2C_SPEED_HZ            100000
#define DDS_I2C_TIMEOUT_MS          50
#define DDS_STATUS_VERSION         2U
#define DDS_STATUS_MAGIC           0xA5U
#define DDS_STATUS_BYTES           15U
#define DDS_SWITCH_OUT             0x01U
#define DDS_SWITCH_DUMMY            0x02U
#define DDS_SERVICE_MS             250U
#define DDS_COMMAND_SPACING_MS     15U
#define DDS_LV_BOOT_MS             1000U /* TODO: verify card boot time. */
#define DDS_CARD_READY_TIMEOUT_MS  5000U
#define DDS_HV_SETTLE_MS            500U /* TODO: verify converter settling. */
#define DDS_SHUTDOWN_TIMEOUT_MS   65000U
/* Contract for RP2040 firmware; GET_STATUS refreshes its command timer. */
#define DDS_COMMAND_TIMEOUT_MS    3000U /* TODO: confirm communication timeout. */
#define DDS_RAMP_TARGET_UA_S         95U

/* TODO: assign from final schematic. -1 deliberately blocks HV enable.
 * TODO: confirm active-high DISABLE (single pull-down NPN assumption).
 * Set CONFIRMED only after checking pin assignments AND polarity.
 * Hardware must hold HV off during reset, before firmware can drive GPIOs. */
#define PIN_CH1_HV_DISABLE          (-1)
#define PIN_CH2_HV_DISABLE          (-1)
#define PIN_CH3_HV_DISABLE          (-1)
#define PIN_CH4_HV_DISABLE          (-1)
#define CH_HV_OFF_LEVEL              1
#define CH_HV_CONTROL_CONFIRMED      0

#if !defined(__cplusplus)
_Static_assert(DDS_MAX_AMPLITUDE_UA == 5000U, "Phase 1 current limit");
_Static_assert(DDS_RAMP_TARGET_UA_S <= 100U, "Ramp exceeds 0.1 mA/s");
_Static_assert(DDS_SHUTDOWN_TIMEOUT_MS >= 60000U, "Shutdown needs at least 60 s");
_Static_assert(DDS_SHUTDOWN_TIMEOUT_MS >
               DDS_MAX_AMPLITUDE_UA * 1000U / DDS_RAMP_TARGET_UA_S + DDS_COMMAND_TIMEOUT_MS,
               "Shutdown must cover full-scale ramp and communication timeout");
#endif

// ============================================================================
// ESP32-S3 PIN CONFIGURATION
// ============================================================================
// Target: ESP32-S3-DevKitC-1-N8R8
//
// CRITICAL: GPIO 26-32 are used by Flash/PSRAM - DO NOT USE
// ============================================================================

// ============================================================================
// BUTTON INPUT PINS
// ============================================================================
#define PIN_SOFT_STOP_PB        9   // GPIO INPUT  - Soft stop button

// ============================================================================
// CHANNEL LOW VOLTAGE ENABLE PINS
// ============================================================================
#define PIN_CH1_LV_EN              4  // GPIO OUTPUT  - Channel 1 LV enable
#define PIN_CH2_LV_EN              5  // GPIO OUTPUT  - Channel 2 LV enable
#define PIN_CH3_LV_EN              6  // GPIO OUTPUT  - Channel 3 LV enable
#define PIN_CH4_LV_EN              7  // GPIO OUTPUT  - Channel 4 LV enable

// ============================================================================
// I2C BUS 0 - DDS MASTER (four XIAO RP2040 cards, 0x50-0x53) [I2C_NUM_0]
// ============================================================================
// RP2040 Zero DDS slave link. Initialise with dds_master_init_new_bus().
#define PIN_DDS_SCL              18   // I2C0 Clock — RP2040 Zero DDS slave bus
#define PIN_DDS_SDA              8   // I2C0 Data  — RP2040 Zero DDS slave bus

// ============================================================================
// I2C BUS 1 — OLED DISPLAY (SSD1306)  [I2C_NUM_1]
// ============================================================================
#define PIN_OLED_SCL            17  // I2C1 Clock
#define PIN_OLED_SDA            16  // I2C1 Data

// ============================================================================
// ADC INPUT PINS
// ============================================================================
#define PIN_V_BAT_MEAS          1   // ADC1_CH0   - Battery voltage measurement

// ============================================================================
// PIN VALIDATION MACROS
// ============================================================================

/** ESP32-S3 has no input-only pins (unlike original ESP32) */
#define IS_INPUT_ONLY_PIN(pin)  (0)

/**
 * Strapping pins — require care during boot
 * GPIO 0:  Boot mode (BOOT button on DevKitC-1)
 * GPIO 3:  JTAG
 * GPIO 45: VSPI
 * GPIO 46: ROM log messages enable
 */
#define IS_STRAPPING_PIN(pin)   ((pin) == 0 || (pin) == 3 || (pin) == 45 || (pin) == 46)

/**
 * UART0 pins — used by USB-UART bridge on DevKitC-1
 * GPIO 43: U0TXD
 * GPIO 44: U0RXD
 */
#define IS_UART_PIN(pin)        ((pin) == 43 || (pin) == 44)

/**
 * Native USB pins — DO NOT reconfigure
 * GPIO 19: USB D-
 * GPIO 20: USB D+
 */
#define IS_USB_PIN(pin)         ((pin) == 19 || (pin) == 20)

/**
 * Flash/PSRAM pins — DO NOT USE
 * GPIO 26-32: SPI Flash and PSRAM (not broken out)
 */
#define IS_FLASH_PSRAM_PIN(pin) ((pin) >= 26 && (pin) <= 32)



// ============================================================================
// PIN REFERENCE — ESP32-S3-DevKitC-1-N8R8 (STABLE CONFIG)
// ============================================================================
/*
 * LEFT SIDE (top to bottom):
 * ──────────────────────────
 * 3V3
 * 3V3
 * RST
 * GPIO4   ADC1_3  TOUCH4    PIN_CH1_LV_EN
 * GPIO5   ADC1_4  TOUCH5    PIN_CH2_LV_EN
 * GPIO6   ADC1_5  TOUCH6    PIN_CH3_LV_EN
 * GPIO7   ADC1_6  TOUCH7    PIN_CH4_LV_EN
 * GPIO15  ADC2_4            SPARE
 * GPIO16  ADC2_5            PIN_OLED_SDA  ← I2C_NUM_1
 * GPIO17  ADC2_6            PIN_OLED_SCL  ← I2C_NUM_1
 * GPIO18  ADC2_7            PIN_DDS_SCL   ← I2C_NUM_0 (Master Bus)
 * GPIO8   ADC1_7  TOUCH8    PIN_DDS_SDA   ← I2C_NUM_0 (Master Bus)
 * GPIO3   ADC1_2  TOUCH3    SPARE
 * GPIO46  LOG               Strapping pin (spare)
 * GPIO9   ADC1_8  TOUCH9    PIN_SOFT_STOP_PB
 * GPIO10  ADC1_9  TOUCH10   SPARE
 * GPIO11  ADC2_0  TOUCH11   SPARE
 * GPIO12  ADC2_1  TOUCH12   SPARE
 * GPIO13  ADC2_2  TOUCH13   SPARE
 * GPIO14  ADC2_3  TOUCH14   SPARE
 * 5V0
 * GND
 *
 * RIGHT SIDE (top to bottom):
 * ───────────────────────────
 * GND
 * GPIO43  U0TXD             UART TX (USB-Serial Bridge)
 * GPIO44  U0RXD             UART RX (USB-Serial Bridge)
 * GPIO1   ADC1_0  TOUCH1    PIN_V_BAT_MEAS
 * GPIO2   ADC1_1  TOUCH2    SPARE
 * GPIO42  MTMS              SPARE
 * GPIO41  MTDI              SPARE
 * GPIO40  MTDO              SPARE
 * GPIO39  MTCK              SPARE
 * GPIO38  FSPIWP            SPARE
 * GPIO37  SPIDQS            PSRAM (RESERVED)
 * GPIO36  SPIIO7            PSRAM (RESERVED)
 * GPIO35  SPIIO6            PSRAM (RESERVED)
 * GPIO0   BOOT              Boot Button (Strapping)
 * GPIO45  VSPI              SPARE (Avoid at boot - Strapping)
 * GPIO48  SPICLK_N          SPARE
 * GPIO47  SPICLK_P          SPARE
 * GPIO21  RTC               SPARE
 * GPIO20  USB_D+            Native USB (RESERVED)
 * GPIO19  USB_D-            Native USB (RESERVED)
 * GND
 *
 * SPARE GPIOs: 2, 3, 10, 11, 12, 13, 14, 15, 21, 38, 39, 40, 41, 42, 45, 46, 47, 48
 *
 * RESTRICTED:
 * GPIO 19-20  Native USB
 * GPIO 26-32  Flash / PSRAM
 * GPIO 43-44  UART0
 * GPIO 0, 3, 45, 46  Strapping Pins
 */
#endif // GPIO_PINS_H
