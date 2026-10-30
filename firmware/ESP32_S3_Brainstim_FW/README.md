# ESP32-S3 Brainstim Controller — Phase 1

Firmware by Peter Zhao.

Main controller for up to four RP2040 DDS stimulation cards. Receives text
commands over BLE (device `R2D2`, Nordic UART Service), sequences each channel's
LV/HV rails, relays settings to the cards over I2C and shows status on an SSD1306
OLED.

Read [the Phase 1 handoff](docs/phase1.md) before building or wiring hardware.
It defines the GET_STATUS v2 protocol shared with
[`../RP2040_DDS_FW`](../RP2040_DDS_FW/README.md); both firmwares must use the same
version. A summary with command and pin tables is in
[`docs/Firmware.md`](../../docs/Firmware.md) at the repository root.

Target: ESP32-S3-DevKitC-1-N8R8, ESP-IDF 5.4.

```
idf.py build flash monitor
```

Host simulations (no hardware; needs Node and Clang with wasm-ld):

```
node tests/run_phase1.mjs
```

| Folder | Contents |
|---|---|
| `main/` | Firmware: BLE (`main.c`), channel FSM, I2C DDS master, GPIO, OLED, battery ADC |
| `web/` | Chrome Web Bluetooth test page (`ble_test.html`) and TypeScript controller |
| `mobile/` | Capacitor Android app wrapping the web UI |
| `tests/` | WebAssembly host tests with mocked ESP-IDF and Pico SDK headers |
| `docs/` | `phase1.md` handoff; `dds_upgrade.md` (superseded) |
| `.phase1-baseline/` | Pre-Phase-1 sources for comparison |

**Before any stimulation:** assign `PIN_CH*_HV_DISABLE` and confirm polarity in
`main/gpio_pins.h`, then set `CH_HV_CONTROL_CONFIRMED`. Until then the firmware
refuses to enable HV. `BLE_TEST_NO_SECURITY` in `main/main.c` is 1 (no pairing)
for desktop testing; set it to 0 for Android use.

Not yet built with ESP-IDF, flashed or tested on hardware.
