CHANNEL ADC / ISOLATED SAMPLER — REV C BENCH PROTOTYPE

This board measures one isolated channel's current and differential port voltage. Replicate it for each channel. It is DC coupled for the requested 0.5 Hz–40 kHz range. Firmware and assembled analogue performance must be verified on dummy loads. Gerbers and DRC do not establish clinical suitability.

FINAL CIRCUIT
The ADS8353IPW uses its INTERNAL 2.5 V references. The former OPA2350 external reference drivers are removed. Each reference has a 22 µF X7R 1210 reservoir through a 0.1 Ω series resistor, following ADS8353 section 11. No parallel capacitor bypasses these resistors. Select fitted capacitors with at least 10 µF effective capacitance at the applied DC voltage. ADC analogue and digital supplies also have 22 µF reservoirs and 100 nF bypass capacitors.

I_MON and V_MON each enter a 2 kΩ/2 kΩ divider. Its 1 kΩ Thevenin resistance plus 470 Ω reproduces the 1.47 kΩ first filter resistor. U201/U202 OPA2320 each implement two unity-gain Sallen-Key stages: effective R1 = R2 = 1.47 kΩ, feedback capacitor 1.2 nF, shunt capacitor 1 nF; then 604 Ω/604 Ω, 6.8 nF/1 nF. Use 0.1% resistors and 1% C0G capacitors, matched between I/V paths. Each ADC input has an 8.2 nF differential charge bucket and 10 Ω in each lead.

Nominal low-frequency scaling:
  I_ADC = 1.024 V + 162 V/A × I_port.
  V_ADC = 1.024 V + V_port/40.
At ±5 mA, I_ADC spans 0.214–1.834 V. At ±30 V, V_ADC spans 0.274–1.774 V, within the ADC's 0–2.5 V range. Ideal current LSB is 0.2355 µA and port-voltage LSB is 1.5259 mV. Decode straight binary as Vin = code × Vref/65536, then apply measured offset and gain. Resolution is not absolute accuracy.

TI's official OPAx320 family macro-model runs in ngspice. At 39.81 kHz it gives gain 0.500563 and phase −63.795°. The ideal 40 kHz result is 0.499494 and −63.625°. A 40 kHz, 5 mA-equivalent transient with deliberately injected 100 pC kicks remains bounded at 0.200–1.839 V. The Q1 family model linked from the OPA320 product page is a nominal proxy, not a tolerance-corner certification of the fitted OPA2320AID. The ADS8353 TINA archive is binary and cannot run in ngspice; the kick test does NOT prove 16-bit settling. Measure noise, distortion, settling and relative I/V phase before using impedance results. The core's protected current-input path adds phase not canceled by these matched filters. See ADC_SPICE_Results.json.

STARTUP AND POWER
Keep LOCAL_READY low at boot and on acquisition faults. Write 0x8040 in a complete valid ADS8353 frame: 32-clock dual-SDO, single-ended, internal reference, 1×Vref, straight binary. Read configuration back. Do not select 2×Vref. Internal references are OFF by default, so preconfiguration codes are invalid. Wait at least 50 ms after configuration/readback, reject initial samples and validate the stream before readiness. The datasheet gives 8 ms typical reference settling with 10 µF; 50 ms is a prototype allowance for the larger reservoirs, not a guaranteed worst-case settling specification.

No external source drives REFIO during AVDD collapse. Reference capacitors follow the supported internal-reference topology. Optional reference-to-Pico ADC dividers were removed to eliminate capacitor backfeed into unpowered Pico GPIO; GP26/GP27 are NC. Test normal removal, brownout and reconnect on the assembled bench board; an arbitrary shorted-supply fault is not certified by this analysis. All ADC input drivers and AVDD share P5_CH; ADC DVDD and SPI share PICO_3V3. The divider/filter resistors limit an independently powered monitor's injection into an unpowered OPA2320 below its documented 10 mA limit. Core status uses Ioff-capable U209, and U208 supports partial power down.

PICO_3V3 is separate from P3V3_CH. Never parallel their regulator outputs. The typical ADC plus four-amplifier quiescent load is 14.3 mA at 5 V; the datasheet static maximum is 16.8 mA, excluding output drive and digital loads. Keep the 45 mA analogue plus 100 mA Pico/local-digital allocation: 145 mA at 5 V until measured. This fits the updated TMR3-0511 auxiliary supply allocation. It is not a battery-runtime result or a guaranteed Pico maximum for all firmware.

HEADERS AND ISOLATION
J201: Samtec TSW-108-07-G-S, unpolarized 1×8, 2.54 mm. Pins: 1 HOST_3V3; 2 HOST_GND; 3 RUN; 4 ARM; 5 HEARTBEAT; 6 FAULT_OK; 7 HOST_UART_TX; 8 HOST_UART_RX. Pin 1 is the square pad.
J202: Samtec TSW-107-07-G-D, unpolarized 2×7, 2.54 mm. Pins: 1 P5_CH; 2 MID_CH_GND; 3 P3V3_CH; 4 MID_CH_GND; 5 I_MON; 6 V_MON; 7 NC; 8 RUN_REQUEST; 9 ARM_EDGE; 10 HEARTBEAT; 11 FAULT_CLEAR_N; 12 INHIBITED; 13/14 NC.
MID_CH_GND means the same channel core MID_CH and remains isolated from HOST_GND. Take conditioned FAULT_CLEAR_N from the revised core J6 pin 1. Use a deliberate harness adapter: J202 is not pin compatible with old separate sense/control headers. Remove former direct RUN/ARM/heartbeat drivers to avoid contention. The existing Howland PCB remains untouched; the prototype uses external relay disconnection.

Pico SC0915 physical pins: 1/2 = GP0/GP1 UART TX/RX; 4 = GP2 ADC_CS; 5 = GP3 SCLK; 6 = GP4 SDI; 7 = GP5 SDO_A; 9 = GP6 SDO_B; 10 = GP7 LOCAL_READY; 11 = GP8 recovered-fault input; 12 = GP9 INHIBITED input. VSYS pin 39 receives isolated 5 V; pin 36 supplies PICO_3V3. VBUS 40, 3V3_EN 37 and ADC_VREF 35 are NC. Fit the Pico in two 20-position 2.54 mm sockets and follow footprint pin 1. Do not connect USB or grounded SWD during isolated operation. Program with channel wiring disconnected and use isolated UART at runtime.

ISO7741F carries RUN, ARM, heartbeat and recovered-fault status; ISO7721F DW16 carries UART. F-suffix default-low behavior removes permissions on opposite-side power loss. U208 ANDs host RUN with LOCAL_READY. This adds no independent local-MCU watchdog: the host must withdraw RUN for stale/invalid telemetry, while the core hardware watchdog and comparators remain independent. Hold ARM low during FAULT_CLEAR_N low and use a fresh edge after stable recovery.

FIRMWARE AND MEASUREMENT
Use deterministic PIO/DMA and verify timing with a logic analyzer. At 600 kSPS and 40 kHz there are 15 samples/cycle. Raw simultaneous I/V payload is 2.4 MB/s; a 1 Mbaud 8N1 UART carries at most 100 kB/s before protocol overhead. Send local amplitude/phase/RMS/DC/quality metrics and selected bursts. A 0.5 Hz cycle lasts two seconds; accumulate phasors incrementally instead of storing a full raw cycle. Reject framing errors, stale sequence/CRC, missing samples, clipping and transitions. A sampled maximum can miss a sine peak by about 2.19%; calibrated fitting is preferable.

Do not classify disconnection from instantaneous near-zero sine current. Qualify commanded excitation and evaluate measured voltage/current together. High voltage with low current can mean disconnection, high impedance or insufficient compliance. Whole-cycle discrimination at 0.5 Hz can take two seconds. Complex impedance is calibrated V1/I1 and is invalid near zero current, clipping, transients or large fit residuals.

ORDER AND BRING-UP
Board: 125×85 mm, two copper layers, original 0.41116 mm nominal thickness. Use the manufacturer's 0.4 mm order option, not 1.6 mm. Use plastic M2.5 supports in the four corner holes. Vias are tented on both faces. A full-height 3.6 mm track/via/zone keepout and 3.5 mm native domain-clearance rule separate host and channel. PCB_Validation.json records the measured copper gap; assembled power-converter/wiring isolation still needs testing.

The JLCPCB ZIP contains bare-board Gerbers and PTH/NPTH drills only. Native sources, BOM, assembly references and verification reports are separate. It does not include assembly, Pico, sockets or firmware. Verify fitted bulk capacitance, rail noise, startup inhibition and calibrated I/V response on dummy loads before drawing performance conclusions.

Primary sources:
https://www.ti.com/lit/ds/symlink/ads8353.pdf
https://www.ti.com/lit/ds/symlink/opa320.pdf
https://www.ti.com/lit/ds/symlink/iso7741.pdf
https://www.ti.com/lit/ds/symlink/iso7721.pdf
https://www.samtec.com/products/tsw-108-07-g-s
https://www.samtec.com/products/tsw-107-07-g-d
https://pip.raspberrypi.com/categories/610-raspberry-pi-pico
