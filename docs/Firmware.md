# BrainStimulation Firmware — Phase 1

Written by Peter Zhao. Source: [`firmware/`](../firmware/).
Detailed handoff and wire contract: [`firmware/ESP32_S3_Brainstim_FW/docs/phase1.md`](../firmware/ESP32_S3_Brainstim_FW/docs/phase1.md)
— that file is the source of truth; this page summarizes it.

**Status: builds and host simulations pass. Nothing has been flashed, scoped or
tested on the analog hardware.** Stimulation is blocked in firmware until the HV
control pins are assigned (see [TODOs](#open-todos)).

## ARCHITECTURE

```
 Phone / Chrome ──BLE (Nordic UART, text)──► ESP32-S3 controller ──I2C0 100 kHz──► up to 4 × RP2040 DDS cards
 mobile/ app                                  firmware/ESP32_S3_Brainstim_FW        firmware/RP2040_DDS_FW
 web/ble_test.html                            · BLE command parser                   addr 0x50–0x53
                                              · 4-channel FSM, soft stop             · DMA-paced SPI → AD5662 DAC
                                              · LV/HV rail sequencing                · 312.5 kS/s waveform tables
                                              · OLED status (I2C1)                   · current ramp ≤ 95 µA/s
                                              · battery ADC                          · OUT/DUMMY PhotoMOS sequencing
```

One RP2040 card drives one stimulation channel. The ESP32 discovers cards at boot;
missing slots are reported `NOT_PRESENT`. Hot-plug is not supported.

| Project | Target | Toolchain | Main files |
|---|---|---|---|
| `ESP32_S3_Brainstim_FW` | ESP32-S3-DevKitC-1-N8R8 | ESP-IDF 5.4, NimBLE | `main/main.c`, `fsm.c`, `i2c_dds_master.c`, `gpio_pins.h` |
| `RP2040_DDS_FW` | Seeed XIAO RP2040 | Pico SDK 2.2.0, Arm GNU 14.2 | `main.c`, `spi_dac.c`, `dac_sync.pio`, `i2c_slave.c`, `dds_state.h` |
| `mobile/` | Android (Capacitor 8) | Node ≥ 22, TypeScript | `src/`, `scripts/build.mjs` |
| `web/` | Chrome desktop (Web Bluetooth) | esbuild | `ble_test.html`, `controller.ts` |

## LIMITS

| Parameter | Value | Defined in |
|---|---|---|
| Frequency | 500 Hz – 30 kHz | `gpio_pins.h`, `dds_state.h` |
| Peak current | 0 – 5.000 mA | same |
| Ramp rate | ~94.9 µA/s (52.7 s to full scale), both directions | `dds_state.h` |
| Sample rate | 312,500 S/s (10.4 samples per 30 kHz cycle) | `dds_state.h` |
| Waveforms | sine, square, triangle, sawtooth | |
| Command timeout | 3 s without a command or GET_STATUS → ramp down, latch fault | both |
| Shutdown confirm | 65 s, then FAULT latched (rails stay up, retries continue) | `gpio_pins.h` |

Limits are enforced by `_Static_assert`s at compile time and range-checked on
both the ESP32 (rejects) and RP2040 (clamps). Current values are computed
setpoints, **not measurements**.

## BLE COMMANDS

Device name `R2D2`, Nordic UART Service. Write text to RX, responses arrive as
TX notifications. Several `DDS …` commands may be sent in one packet.
Channels are numbered `0`–`3`; `ALL` broadcasts to every present card.

| Command | Example | Response |
|---|---|---|
| `DDS <ch> FREQ <Hz>` | `DDS 0 FREQ 1000` | `DDS OK S0 FREQ=1000.000` |
| `DDS <ch> WAVE <name>` | `DDS ALL WAVE SINE` | `DDS OK ALL WAVE=SINE` |
| `DDS <ch> AMP <mA>` | `DDS 1 AMP 2.5` | `DDS OK S1 AMP=2.500` |
| `DDS <ch> ON` | `DDS 0 ON` | `DDS OK S0 ON` |
| `DDS <ch> OFF` | `DDS 0 OFF` | `DDS OK S0 OFF` |
| `DDS <ch> STATUS` | `DDS 0 STATUS` | `DDS S0 FREQ=… WAVE=… AMP=… STATE=… CURRENT=… OUT=… DUMMY=…` |

Errors start with `DDS ERR`. After a soft stop, BLE disconnect, battery-critical
or channel fault, only `OFF` and `STATUS` are accepted until the device is
restarted. Anything without a `DDS ` prefix is echoed back (`ECHO: …`).

> `BLE_TEST_NO_SECURITY` is currently **1** in `main/main.c` (no pairing, for Chrome
> desktop testing). Set it to 0 for the Android build with encryption.

## ESP32 ↔ RP2040 I2C PROTOCOL (v2)

100 kHz, addresses `0x50`–`0x53` (RP2040 reads its address from GPIO 26–28).

| ID | Command | Payload |
|---|---|---|
| `0x01` | SET_FREQ | frequency |
| `0x02` | SET_WAVEFORM | 0 sine, 1 square, 2 triangle, 3 sawtooth |
| `0x03` | SET_AMPLITUDE | peak µA |
| `0x04` | SET_ENABLE | 0/1 |
| `0x05` | SET_ALL | 9 bytes total incl. command |
| `0x06` | GET_STATUS | repeated START, read **15 bytes** (below) |
| `0xFF` | RESET | ramp down; clears latched fault once OFF |

GET_STATUS reply, little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | frequency, mHz |
| 4 | 2 | requested amplitude, µA |
| 6 | 1 | waveform |
| 7 | 1 | enable intent |
| 8 | 1 | faults: 1 comm timeout, 2 stream, 4 watchdog reboot |
| 9 | 1 | protocol version = 2 |
| 10 | 1 | state: 0 OFF, 1 PRECHARGE, 2 CONNECT, 3 RAMP_UP, 4 ACTIVE, 5 RAMP_DOWN, 6 DISCONNECT |
| 11 | 2 | applied amplitude, µA (rounded up) |
| 13 | 1 | bit 0 OUT_EN, bit 1 DUMMY_EN |
| 14 | 1 | marker `0xA5` |

The ESP32 rejects other versions/markers. **Both firmwares must be updated together.**

## SAFETY SEQUENCING

**Power-up (ESP32, per channel):** LV on → 1 s card boot → valid OFF status
(≤ 5 s) → HV on → 500 ms settle → ready.

**Output enable (RP2040):** DUMMY on → ≥ 1 s precharge → OUT on at zero →
10 ms overlap → DUMMY off → ramp up to target.

**Output disable / reset / timeout:** ramp to zero → drain DMA + SPI pipeline →
DUMMY on → 10 ms → OUT off → 10 ms → DUMMY off → publish OFF.

**Shutdown (ESP32):** confirm each card OFF, then HV off before LV off.
No silent forced cutoff; after 65 s unconfirmed, FAULT is latched.

**Faults:** a missed DMA refill latches a stream fault and stops watchdog pets;
the 500 ms watchdog reboots the card with switches disconnected and DAC at midpoint.

## PIN MAPS

**ESP32-S3-DevKitC-1-N8R8** (`main/gpio_pins.h`, `pin_map_final.txt`)

| Function | GPIO | Notes |
|---|---|---|
| DDS I2C SCL / SDA (I2C0) | 18 / 8 | needs 2.2 kΩ external pull-ups |
| OLED SCL / SDA (I2C1) | 17 / 16 | SSD1306 |
| CH1–CH4 LV enable | 4, 5, 6, 7 | |
| CH1–CH4 HV disable | **unassigned (-1)** | blocks stimulation until set |
| Soft stop button | 9 | |
| Battery voltage | 1 (ADC1_0) | divider R1 = 30 kΩ, R2 = 10 kΩ |
| Reserved | 0, 3, 45, 46 strap · 19–20 USB · 26–32 flash/PSRAM · 43–44 UART | |

**Seeed XIAO RP2040** (`dds_state.h`)

| Function | GPIO | Notes |
|---|---|---|
| DAC SPI SCK / MOSI / SYNC | 2 / 3 / 4 | AD5662, 20 MHz, SYNC framed by PIO |
| I2C SDA / SCL | 6 / 7 | slave, 100 kHz |
| Address select | 26, 27, 28 | → 0x50 + n |
| OUT_EN / DUMMY_EN | 0 / 1 | **TODO** confirm against schematic |
| Reserved | 29 (A3) | never configured |

## BUILD AND TEST

```sh
# RP2040 (from firmware/RP2040_DDS_FW; set PICO_SDK_PATH, PICO_TOOLCHAIN_PATH)
cmake -S . -B build-phase1 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-phase1            # add -DPICO_NO_PICOTOOL=1 if picotool is missing

# ESP32-S3 (from firmware/ESP32_S3_Brainstim_FW, ESP-IDF 5.4 environment)
idf.py build flash monitor

# Host simulations (from firmware/ESP32_S3_Brainstim_FW; needs Node + Clang with wasm-ld)
node tests/run_phase1.mjs

# Android app (from firmware/ESP32_S3_Brainstim_FW/mobile)
npm install && npm run add:android && npm run open:android
```

Keep `ESP32_S3_Brainstim_FW` and `RP2040_DDS_FW` side by side — the host tests
load the RP2040 sources from `../RP2040_DDS_FW`. Build directories are git-ignored.

| Verified | Not yet verified |
|---|---|
| RP2040 Release build links (XIAO RP2040) | ESP-IDF full build (toolchain absent on build machine) |
| Host tests: shutdown, timeout, ramp, I2C framing, fault/retry, web UI | DMA timing, SYNC, 30 kHz output quality on a scope |
| Web scripts parse | NimBLE/FreeRTOS integration, GPIO electrical behavior |
| | Any flashing, analog or load testing |

## OPEN TODOS

1. **Assign HV disable pins** and confirm polarity in `main/gpio_pins.h`, then set
   `CH_HV_CONTROL_CONFIRMED 1`. Stimulation is refused until this is done.
2. Confirm RP2040 `OUT_EN_PIN` / `DUMMY_EN_PIN` against the final schematic.
3. Confirm provisional timings: 1 s card boot, 500 ms HV settle, 3 s command
   timeout, 10 ms PhotoMOS release.
4. Review the 65 s fault policy against the system hazard analysis.
5. **DAC mismatch:** this firmware drives an **AD5662** on a XIAO RP2040; the
   Signal Generator Rev C board uses a **DAC8830** with a Pico 2 W
   (`boards/Signal_Generator/FIRMWARE_COMPATIBILITY.txt`). Decide which hardware
   the next build targets.
6. Set `BLE_TEST_NO_SECURITY 0` before any non-bench use.
7. Bench-verify everything in the "not yet verified" column on a 2–5 kΩ dummy load.

## OTHER FILES

| Path | Purpose |
|---|---|
| `ESP32_S3_Brainstim_FW/.phase1-baseline/` | Pre-Phase-1 copies of the changed sources, kept for comparison |
| `ESP32_S3_Brainstim_FW/docs/dds_upgrade.md` | Superseded notes; redirects to `phase1.md` |
| `ESP32_S3_Brainstim_FW/pin_map_final.txt` | ASCII DevKitC pin diagram |
| `ESP32_S3_Brainstim_FW/web/dist/` | Built web UI (regenerate with `npm run build:web` in `mobile/`) |
