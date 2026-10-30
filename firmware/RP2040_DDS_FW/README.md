# RP2040 DDS Phase 1

Firmware by Peter Zhao.

This daughter-card firmware uses timer-paced DMA into SPI0 at 312,500 samples/s,
PIO-controlled AD5662 SYNC, midpoint-centered waveforms, timed current ramps and
PhotoMOS output sequencing. Core 1 handles the isolated I2C slave.

Read [the cross-project handoff](../ESP32_S3_Brainstim_FW/docs/phase1.md) before
building or configuring hardware. It defines the 15-byte GET_STATUS v2 packet,
hardware TODOs, timing assumptions and validation limits. Both firmwares must
use this protocol version.

Build with Pico SDK 2.2.0 and a new build directory; the existing build directory
may contain paths from a different computer:

```
cmake -S . -B build-phase1 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-phase1
```

Set PICO_SDK_PATH and PICO_TOOLCHAIN_PATH to local installations. If picotool is
unavailable, add -DPICO_NO_PICOTOOL=1 (ELF/BIN/HEX only). Firmware has not been
flashed or validated on the analog hardware.
