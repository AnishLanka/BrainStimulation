# 30 kHz / 5 mA commands and the RP2040 DMA upgrade

This repository contains the ESP32-S3 controller, not the RP2040 waveform
engine. The controller now accepts requests up to **30 kHz and 5 mA peak**.
This change does not establish that the connected output can deliver them.
RP2040 DMA and analog-output validation remain outstanding.

## Where the limits were

| Layer | Previous maximum | Updated command maximum |
| --- | --- | --- |
| `web/ble_test.html`: sliders, number inputs, JavaScript | 10 kHz / 2 mA | 30 kHz / 5 mA |
| `main/main.c`: BLE FREQ / AMP parser | 20 kHz / 65.535 mA | 30 kHz / 5 mA |
| `main/i2c_dds_master.c`: individual setters and SET_ALL | No numeric range validation | 30 kHz / 5 mA |

Firmware limits are declared in `main/i2c_dds_master.h`. The BLE parser also
rejects NaN, infinity, empty values, numeric overflow and trailing garbage;
the I2C driver validates numeric ranges before conversion to unsigned integers.
The web slider retains its existing 500 Hz minimum. Raw BLE frequency commands
still support 0.001 Hz, subject to slave capability and achievable precision.

The existing wire protocol already has room for the new requests:

```text
DDS 0 FREQ 30000 -> CMD 0x01, uint32_t 30000000 mHz -> 01 80 C3 C9 01
DDS 0 AMP 5      -> CMD 0x03, uint16_t 5000 uA     -> 03 88 13
```

`dds_master_set_all()` uses those same fields in its packed nine-byte packet.
No command IDs or packet sizes need changing. The slave must validate the
same limits and update its calibration and generation logic as appropriate.
An I2C acknowledgement or status echo confirms neither measured frequency nor
measured output current. The OLED displays cached requested values.

## What DMA changes

The path through this repository is:

```text
Browser -> BLE -> process_dds_command() -> dds_master_set_*()
        -> I2C command -> RP2040 waveform engine -> DAC -> current driver
                         ^ firmware missing here
```

I2C carries parameters, not each waveform sample. Its 100 kHz clock is not the
waveform sample rate; raising that clock is not the way to obtain 30 kHz output.

A lookup table and DMA work together. Generate or scale a sample buffer when
parameters change, then let DMA move samples from RAM to the DAC peripheral.
This removes repeated CPU sample handling. DMA does not calculate sine values
or turn a current setting into a calibrated DAC code.

Illustrative RP2040 structure (pseudocode, not integrated or compiled firmware):

```c
on_parameters_changed(request) {
    validate_request(request);
    prepare_inactive_dac_buffer(request); // LUT + calibrated gain/offset
    schedule_buffer_swap_at_boundary();  // never edit DMA's active buffer
}

configure_output() {
    configure_pio_dac_timing(sample_rate); // includes CS/LDAC and frame timing
    configure_data_dma(buffer, pio_tx_fifo, pio_tx_dreq);
    configure_control_dma_to_reload_data_dma();
    start_output();
}

main_loop() {
    service_pending_commands();
    wait_for_interrupt(); // keep DMA/PIO clocks running; avoid busy-waiting
}
```

Raspberry Pi's [SPI DMA example](https://github.com/raspberrypi/pico-examples/blob/master/spi/spi_dma/spi_dma.c)
demonstrates FIFO-paced transfers. Its
[DMA control-block example](https://github.com/raspberrypi/pico-examples/blob/master/dma/control_blocks/control_blocks.c)
demonstrates a second channel reloading transfer counts and source addresses.
Those mechanisms are starting points, not a drop-in driver for an unknown DAC.

For continuous output, implement and verify a repeating reload sequence.
Address ring wrapping alone does not restart an exhausted transfer count.
Avoid a CPU interrupt for every sample, or even every waveform period at
30 kHz. PIO can provide sample timing and DAC framing; SPI TX DREQ alone only
tracks FIFO space. The chosen peripheral must enforce the intended sample
interval and the DAC's CS/LDAC requirements. OFF and fault handling must stop
the repeating DMA chain, handle queued peripheral data, and command the
calibrated zero-current state (which may not be DAC code zero).

For a repeated table containing one waveform period:

```text
sample_rate = samples_per_period * output_frequency
```

| Samples per period | Sample rate at 30 kHz | Minimum serial bit rate for 16 bits/sample |
| --- | --- | --- |
| 32 | 0.96 MS/s | 15.36 Mbit/s |
| 64 | 1.92 MS/s | 30.72 Mbit/s |
| 128 | 3.84 MS/s | 61.44 Mbit/s |
| 256 | 7.68 MS/s | 122.88 Mbit/s |

These are arithmetic budgets, not supported hardware configurations. Add DAC
command/framing overhead and settling time. Choose sample count, output timing
and filtering together; square and sawtooth harmonics require additional
bandwidth. With a fixed sample clock and phase-accumulator DDS, frequency
instead depends on the phase increment. Plain sequential DMA cannot perform
that phase lookup: generate suitable buffers or select a different timing
scheme. Low frequencies also need checking against available clock dividers.

## What 5 mA requires

The AMP command is a requested peak current, not RP2040 GPIO drive strength.
Inspect the actual DAC range, DAC-code-to-current calibration, sense resistor,
amplifier bandwidth and stability, output compliance voltage, supply rails,
component ratings and current limiting. A firmware constant cannot increase
the available voltage or current of the analog stage.

As an electrical load example, 5 mA through 10 kohm requires 50 V across the
load, plus driver headroom. At the same resistive load and waveform, increasing
current from 2 mA to 5 mA multiplies load power by `(5/2)^2 = 6.25`.
Therefore lower RP2040 CPU activity does not guarantee lower total system power.
The current-versus-frequency performance must be measured together.

## Remaining implementation and verification

Provide the RP2040 source (DDS loop, DAC interface, I2C handler and Pico SDK
build files) and DAC/current-driver schematic or part numbers. Then implement
the timed DMA path against the actual interface and enforce limits on the slave.

On a suitable bench load, verify 30 kHz timing and distortion, calibrated 5 mA
peak output without clipping, parameter changes, continuous playback, and
OFF/fault response. Compare RP2040 supply power before/after DMA with identical
waveform, current, load and clock settings, and measure whole-system power
separately. A claim of a significant power reduction needs those measurements.

## Checks performed for the controller change

An isolated host harness compiled the actual C range helpers, individual
frequency/amplitude setters, SET_ALL and broadcast functions to WebAssembly
with mocked I2C writes. It checked both allowed endpoints, rejected negative,
out-of-range and non-finite values, unchanged cache/write count on rejection,
and the little-endian packet bytes above. Node.js checked the complete web
script's syntax and executed the frequency/amplitude handlers to verify
30 kHz / 5 mA command generation and clamping.

These checks do not compile the complete ESP32 application or exercise the
BLE stack. The ESP-IDF installation configured in `.vscode/settings.json`
(`C:/esp/v5.4.3/esp-idf`) was unavailable in the editing environment. No firmware
was flashed and no physical output or power measurements were performed.
