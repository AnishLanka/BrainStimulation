# Phase 1 implementation and handoff

Both firmware projects must be updated together. The ESP32 refuses legacy status
packets and refuses stimulation until HV GPIO assignments/polarity are confirmed.
`sdkconfig` was not modified. Nothing has been flashed.

## Sample timing and output generation

The old RP2040 path did **not** have a fixed sample clock: each iteration called
blocking SPI and petted the watchdog. Both main.c and i2c_slave.c used 313036 as
a frequency-calibration divisor; neither included the conflicting 100000 Hz
definition in dds_state.h. Comments also cited 323819. Source inspection cannot
establish its physical update rate; the old rate must be measured if a before/after
comparison is wanted.

The new path uses SAMPLE_RATE_HZ = 312500, with a 125 MHz system clock. A DMA timer
requests one 12-bit SPI0 word every 200 system clocks (1.6 us); two words form each
24-bit AD5662 frame, yielding a nominal 3.2 us/sample. The six upper command bits
and two power-mode bits are zero during streaming. SPI mode 2 sends data for
sampling on falling SCLK edges. SPI runs at the divider-selected rate near
20 MHz, leaving ample time between words. PIO counts the actual 24 falling edges,
then pulses SYNC high. DMA alternates two 257-sample buffers and interrupts once
per buffer. Core 0 computes the inactive buffer and sleeps between events.

This gives 10.42 samples per 30 kHz cycle. DMA arbitration, clock accuracy,
SYNC timing, analog bandwidth and spectral quality still need bench measurement;
a passing software build does not prove these. The PIO debug output toggles once
per sample (square-wave frequency Fs/2) when SAMPLE_DEBUG_PIN is assigned.
It defaults to -1 because all exposed XIAO pins are allocated and ADC3/GPIO29 is
reserved. SYNC itself is always available as a once-per-sample scope signal.
Do not assign a debug pin until an unused pad is confirmed.

The amplitude scale spans 0..65536 for 0..5000 uA. LUTs are generated once in RAM
at boot with exact mean 32768 and extrema within 1..65535. Scaling uses a signed
midpoint difference and symmetric truncation, keeping zero exactly 32768.
The full-scale mapping assumes the supplied 3.000 V reference and rescaled
Howland stage. Current fields are computed setpoints, **not current measurements**.

The ramp uses the microsecond timer with one Q16 count per 804 us: approximately
94.89 uA/s, 52.69 s for a full-scale ramp. It runs in both directions; block
buffering introduces millisecond-scale command latency and quantized amplitude
steps. Compile-time checks enforce the current/ramp limits and sample-rate budget.
A frequency correction table defaults to unity, interpolates between its anchors,
and caps the resulting scale at DAC full scale. Calibration cannot produce more
than the available DAC swing; values must be validated before changing the table.

## Output sequencing and faults

- Boot: OUT_EN and DUMMY_EN driven low before peripheral setup; DAC brought to
  midpoint as soon as SPI is configured. Watchdog reboot additionally performs
  power-down/wakeup at midpoint.
- Enable: dummy on, at least 1 s precharge, OUT on at zero, 10 ms overlap, dummy
  off, then ramp to target.
- Disable/reset/communication timeout: ramp to zero, wait for both DMA buffers
  and SPI pipeline to contain zero-amplitude samples, dummy on, wait 10 ms,
  OUT off, wait 10 ms, dummy off.
- A parameter change while active uses the same amplitude ramp. Frequency and
  waveform changes preserve phase accumulation but are not phase-slewed.
- A valid command or GET_STATUS refreshes a provisional 3 s communication timer.
  Timeout latches a fault and cannot automatically resume stimulation.
- RESET is a ramp-down request. It clears a latched fault only after OFF.
- A missed DMA refill deadline latches a stream fault and stops watchdog pets.
  The watchdog reboots to disconnected switches. A stalled CPU/DMA cannot promise
  a controlled current ramp; validate hardware failure behavior separately.

Core 1 installs the I2C IRQ on Core 1. Shared command/status structs are copied
under a short IRQ-safe cross-core lock; no lock or blocking call is in the sample
generation loop. Invalid command lengths, oversized transactions and invalid
waveform/enable values are ignored. Frequency/amplitude are clamped on the card;
the ESP32 rejects out-of-range BLE commands before I2C writes.

## GET_STATUS v2 wire contract

Command 0x06, repeated START, read **15 bytes**, little-endian. SET_ALL remains
9 bytes including command 0x05. Other command IDs are unchanged.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | accepted frequency, uint32 millihertz |
| 4 | 2 | requested peak amplitude, uint16 microamps |
| 6 | 1 | waveform: sine 0, square 1, triangle 2, sawtooth 3 |
| 7 | 1 | effective enable intent, 0/1 |
| 8 | 1 | fault bits: communication 1, stream 2, watchdog reboot 4 |
| 9 | 1 | protocol version = 2 |
| 10 | 1 | output state, see below |
| 11 | 2 | conservative applied amplitude, uint16 microamps |
| 13 | 1 | bit 0 OUT_EN, bit 1 DUMMY_EN; remaining bits zero |
| 14 | 1 | format marker = 0xA5 |

States: OFF=0, PRECHARGE=1, CONNECT=2, RAMP_UP=3, ACTIVE=4, RAMP_DOWN=5,
DISCONNECT=6. RAMP_UP also covers tracking a changed active setpoint.
Applied amplitude is the maximum scale in the queued buffers/current ramp,
converted to microamps by rounding **up**. This avoids reporting zero for a
nonzero fractional-microamp output. OFF is published only after drain and switch
sequencing. Switch bits report commanded GPIO states, not physical contact feedback.

ESP32 rejects incompatible versions, wrong markers, out-of-range fields and
inconsistent OFF packets. A safe shutdown confirmation requires a successful
fresh packet with enable=0, applied current=0, state=OFF, OUT=0 and DUMMY=0.
A fault flag can coexist with a verified safe OFF state.

Example BLE response:
`DDS S0 FREQ=30000.000 WAVE=SINE AMP=5.000 STATE=RAMP_DOWN CURRENT=2.123 OUT=1 DUMMY=0`.

## Motherboard control and assumptions

Four channels map to 0x50..0x53. Missing slots are skipped for broadcasts and
reported as NOT_PRESENT for status. An empty board no longer fails driver init.
Present cards that are unready or use the old protocol cannot be enabled.
Discovery is performed at boot; hot-plug recovery is not implemented.

All per-channel HV GPIOs are deliberately -1, and CH_HV_CONTROL_CONFIRMED is 0.
**TODO:** assign pins and confirm polarity in gpio_pins.h; the provisional signal
is active-high DISABLE for a single pull-down NPN. Boot cannot enforce a safe
level on an unassigned or incorrectly wired pin. Hardware must hold HV off
through reset. Pin validation rejects known bus/LV/ADC/USB/flash/PSRAM conflicts.

Power-up is LV on -> provisional 1 s boot wait -> valid OFF status (up to 5 s
per present card) -> HV on -> provisional 500 ms settling -> ready.
OUT/DUMMY assignments GPIO0/GPIO1 follow the supplied brief and remain schematic
TODOs. GPIO29 is never configured. PhotoMOS overlap/release waits are provisional.

The DDS command task owns I2C after startup and polls powered cards every 250 ms,
also serving as the command heartbeat. BLE disconnect, soft-stop, battery-critical
and channel-fault events immediately gate stimulation commands and are handled
without blocking the BLE/OLED tasks. Shutdown independently confirms each card
OFF, then disables that card's HV before its LV. Ordinary per-channel OFF leaves
rails on and ramps/disconnects that output.

After 65 s without confirmation, FAULT is latched; unconfirmed rails stay powered,
OFF/status retries continue, and verified channels can still be powered down.
**TODO:** confirm this policy against the system hazard analysis, especially
permanent communication failure and battery exhaustion. There is no silent
fixed-time forced cutoff.

A BLE disconnect conservatively shuts down and latches STOPPED even if the output
was idle; reconnect does not restore stimulation. STOPPED/EMERGENCY/FAULT require
restart. Previously queued ON commands cannot bypass the latch. Final confirmed
OFF statuses remain available from cache after rail-off. New stimulation requires
a new boot/readiness sequence.

OLED uses two rows per channel with 30.0 kHz / 5.00 mA formatting. The browser has
four channel rows and displays applied current/output state independently of the
requested amplitude slider. Existing 5 mA/30 kHz limits and presets are retained.
Its one-second UI status polls are separate from the firmware heartbeat.

## Validation and remaining bench work

- Pico SDK 2.2.0, Arm GNU 14.2: Release build linked successfully for
  seeed_xiao_rp2040. ELF/BIN/HEX built; picotool/UF2 was disabled for this check.
- Host tests compile actual RP2040 control/LUT/I2C code and actual ESP32 driver/FSM
  to WebAssembly with mocked hardware. Normal shutdown, timeout, early stop, reset,
  timer wrap, midpoint symmetry, range checks, wire bytes, malformed I2C writes,
  repeated START, old/corrupt status, sparse cards, command gating and the 65 s
  fault/retry path pass. Observed simulated ramp windows were about 94.9 uA/s.
- Web scripts parse successfully; per-channel ramp/current and missing-card
  rendering tests pass.
- Run: `node tests/run_phase1.mjs` from ESP32_S3_Brainstim_FW. Requires Node and
  Clang with wasm-ld; CLANG can override the default Windows Clang path.
- Full ESP-IDF build was **not** run: configured C:/esp/v5.4.3/esp-idf is absent.
  Host tests do not validate NimBLE/FreeRTOS integration or GPIO electrical behavior.
- No flashing, scope measurements, analog measurements or patient testing.
  Host tests mock DMA/GPIO; they do not prove DMA timing, pipeline drain or physical
  switch release. Verify 312.5 ksample/s SYNC, optional debug Fs/2, all 24 SPI bits,
  buffer continuity under load, 30 kHz output quality, DAC midpoint during ramps,
  full-scale current and both-direction ramp slope on a bench dummy load.
  Exercise OFF during each sequence state, BLE/I2C loss and watchdog reset.

Sources used for implementation: [AD5662 data sheet](https://www.analog.com/media/en/technical-documentation/data-sheets/AD5662.pdf),
[Pico hardware APIs](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html),
[ESP-IDF 5.4 I2C API](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32s3/api-reference/peripherals/i2c.html).

## Changed-file inventory


### ESP32_S3_Brainstim_FW

- `main/main.c`: Four-channel BLE control, early stop gating, nonblocking DDS worker and ordered boot.
- `main/fsm.c`: Status-driven shutdown, 65 s fault/retry policy, heartbeat and HV readiness.
- `main/fsm.h`: Stop/fault events and worker service interface.
- `main/gpio_helpers.c`: Safe GPIO initialization, pin validation, HV-before-LV power-off.
- `main/gpio_helpers.h`: Per-channel rail/HV control interface.
- `main/gpio_pins.h`: Limits, timing and deliberately unassigned HV pin/polarity configuration.
- `main/i2c_dds_master.c`: Four-card discovery, range validation, v2 status checks and separate applied state.
- `main/i2c_dds_master.h`: Versioned packet layout, state types and driver interface.
- `main/oled_driver.c`: Two rows per channel, live current, state and missing-card display.
- `web/ble_test.html`: Four-channel selection and periodically refreshed applied-current/state rows.
- `docs/dds_upgrade.md`: Redirect obsolete controller-only notes to this handoff.
- `docs/phase1.md`: Protocol, assumptions, tests, limitations and changed-file inventory.
- `tests/.gitignore`: Exclude generated test outputs.
- `tests/esp_harness.c`: ESP32 driver/FSM peripheral and time mocks.
- `tests/i2c_harness.c`: RP2040 I2C callback and packet test adapter.
- `tests/mocks/esp/driver/i2c_master.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/esp/esp_check.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/esp/esp_err.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/esp/esp_log.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/esp/freertos/FreeRTOS.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/esp/freertos/task.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/esp/inttypes.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/esp/math.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/esp/string.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/rp/hardware/clocks.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/rp/hardware/gpio.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/rp/hardware/i2c.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/rp/hardware/sync.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/rp/hardware/watchdog.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/rp/math.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/rp/pico/i2c_slave.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/rp/pico/multicore.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/mocks/rp/pico/stdlib.h`: Minimal SDK/header mock for the host safety tests; not used by firmware builds.
- `tests/rp_harness.c`: RP2040 control/LUT simulation adapter.
- `tests/run_phase1.mjs`: Compile/run firmware host simulations and web checks.

### RP2040_DDS_FW

- `main.c`: Boot safety, output FSM, timer-based ramp, timeout and calibration placeholder.
- `dds_state.h`: All shared limits/timing/pins, protocol constants, math helpers and assertions.
- `dds_shared.c`: IRQ-safe cross-core command/status snapshots.
- `dds_tables.c`: Boot-generated, exactly midpoint-centered waveform tables.
- `dds_tables.h`: RAM table declarations and initialization interface.
- `spi_dac.c`: Timer-paced SPI0 DMA buffers, midpoint boot and stream/drain checks.
- `spi_dac.h`: Boot and streaming DAC interfaces.
- `dac_sync.pio`: 24-edge SYNC framing and optional per-sample debug toggle.
- `i2c_slave.c`: Validated commands, mHz preservation, heartbeat and 15-byte status.
- `i2c_slave.h`: Core 1 I2C initialization/task interface.
- `CMakeLists.txt`: DMA/PIO/shared-state sources, libraries and generated PIO header.
- `README.md`: Build instructions and cross-project handoff link.
