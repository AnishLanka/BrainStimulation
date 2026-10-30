# DDS card firmware — Pico 2 W + DAC8830

Firmware by Peter Zhao. The folder keeps its historical `RP2040_DDS_FW` name
because the host tests and docs reference it; the target since 2026-10-30 is
the **Raspberry Pi Pico 2 W (RP2350)** driving the **DAC8830** on the Signal
Generator Rev C board. The earlier AD5662 / XIAO RP2040 build is not compatible
with that board (see `boards/Signal_Generator/FIRMWARE_COMPATIBILITY.txt`).

The firmware uses timer-paced DMA into SPI0 at 312,500 samples/s, one 16-bit
DAC8830 code per sample in SPI mode 0, PIO-framed chip select (16 falling edges,
then CS high so the DAC loads), midpoint-centered waveforms, timed current ramps
and PhotoMOS output sequencing. Core 1 handles the I2C slave to the ESP32.

## Rev C board interface

| Pico 2 W GPIO | Rev C net | Notes |
|---|---|---|
| GP2 | J1.04 / J2.04 `SPI_SCLK_SYS` | shared |
| GP3 | J1.05 / J2.05 `SPI_MOSI_SYS` | shared |
| GP4 | J1.06 `SPI_CS_SYS` | streamed channel, PIO framed |
| GP5 | J2.06 `SPI_CS_SYS_2` | written to midpoint at boot, then held high |
| GP8 | J1.07 `FAULT_TO_PICO` | ISOW7741 EN/FLT, input only, low = fault |
| GP9 | J2.07 `FAULT_TO_PICO_2` | input only, not yet evaluated |
| GP6 / GP7 | — | I2C1 slave to ESP32 |
| GP26–28 | — | I2C address select |
| GP0 / GP1 | — | OUT_EN / DUMMY_EN placeholders: Rev C has no PhotoMOS; the relay is driven via the ADC board's RUN/ARM/HEARTBEAT host header (see `docs/Firmware.md` TODO 2) |
| GP23/24/25/29 | — | CYW43 radio, never assigned |

The Rev C integration guide states that the controller's GPIO selection belongs
to the firmware, so these GPIO numbers are the assignment the harness must
follow. The first Rev C build uses one channel of one board (J1/J3), which is
what this firmware streams. Both DACs power up at code 0,
which is −VREF on the bipolar output; firmware writes midpoint to both at boot,
but that is not a startup inhibit. Only channel 1 (J1/J3) is streamed; channel 2
is parked at midpoint until a two-channel engine exists.

Read [the cross-project handoff](../ESP32_S3_Brainstim_FW/docs/phase1.md) before
building or configuring hardware. It defines the 15-byte GET_STATUS v2 packet,
hardware TODOs, timing assumptions and validation limits. Both firmwares must
use this protocol version. Fault bit 8 (`FAULT_ISO_SUPPLY`) is new: EN/FLT low
while current could flow ramps down and latches, like a command timeout.

## Build

Pico SDK 2.2.0 (`PICO_BOARD pico2_w` is set in CMakeLists.txt) with a new build
directory; the committed `build/` is from the old XIAO target:

```
cmake -S . -B build-pico2w -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-pico2w
```

Set PICO_SDK_PATH and PICO_TOOLCHAIN_PATH to local installations. If picotool is
unavailable, add -DPICO_NO_PICOTOOL=1 (ELF/BIN/HEX only). Output is
`pico2w_dds.uf2`.

**Status:** the DAC8830 / Pico 2 W retarget has been source-reviewed only. It has
not been compiled against the Pico SDK, flashed, or validated on the analog
hardware. Bench-test on the low-voltage outputs (J3) before connecting the
high-voltage chain.
