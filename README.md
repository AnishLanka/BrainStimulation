# BrainStimulation — Prototype Demo 2 / PCB Package Rev C

By Pythagoras Carayannopoulos, Peter Zhao and Anish Lanka.
Presentation: [`docs/Prototype_Demo_2.pdf`](docs/Prototype_Demo_2.pdf)

Start with `docs/RevC_Release/READ_ME_FIRST.txt` and `docs/RevC_Release/Integration_Guide.html`.
Upload each board's manufacturing ZIP to JLCPCB as a separate bare PCB design.
Do not upload the complete release archive as one PCB.

## TEAM

| Person | Responsibilities |
|--------|------------------|
| Pythagoras Carayannopoulos (Pyth) | Boost, Howland Pump circuit design, soldering, testing |
| Anish Lanka | Rail Splitter, Buck-Boost, Howland Pump schematic and PCB, soldering, testing |
| Peter Zhao | Signal Generator, firmware (ESP32-S3 controller, RP2040 DDS, web and Android apps), testing |

## SYSTEM OVERVIEW

The signal generator sets the shape of the stimulation current. The power path
provides the energy and voltage to push it. The two paths meet at the Howland,
which turns the voltage command into a current.

```
POWER PATH   Battery ──► Buck-Boost ──► Isolated 12 V ──► Boost ──► Rail Splitter
             2.8–3.7 V   steady 3.3 V   new isolated      79.7 V    ±39.85 V about MID
                                        domain                           │ ±40 V
                                                                         ▼
                                                                     Howland ──► External load
                                                                         ▲      electrodes; 2–5 kΩ on bench
                                                                         │ ±3.3 V command
SIGNAL PATH  Pico 2 W ──► ISOW7741 ──────► DAC8830 ──────► OPA2192
             samples      isolates SPI     code→0–3.3 V    bipolar ±3.3 V
                          and power
```

Compared with the Stim the Brim design we built on (~60 V boost, ±30 V rails),
ours uses ~80 V / ±40 V so firmware can limit output to ±30 V without clipping
from rail saturation.

## REPOSITORY LAYOUT

Every board folder follows the same pattern:

```
<Board>/
  *_JLCPCB.zip / *_fabrication.zip   Gerbers — upload this to JLCPCB
  Release_DRC.json                   Rev C release DRC report
  kicad/
    Anish_Work/                      Anish's KiCad files for this board
    Peter_Work/                      Peter's KiCad files for this board
    Pyth_Work/                       Pyth's KiCad files for this board
```

Open the `.kicad_pro` inside a `*_Work/` folder and keep that folder together —
project libraries are referenced relatively. Empty work folders hold a `.gitkeep`.

```
boards/
  Boost/                 Boost_79V7_Prototype_JLCPCB.zip, Boost_FINAL_ASSEMBLY_BOM.csv
    kicad/Pyth_Work/       Boost_Electrical_Draft.kicad_pro (Rev C project)
                           Boost_Convertor_Circuit_Pyth.kicad_sch (working changes)
  Buck_Boost/            BuckBoost_RevB_fabrication.zip
    kicad/Pyth_Work/       BuckBoost_RevB.kicad_pro (Rev C project)
                           BuckBoost_RevBPythWork.zip (working copy of project)
  Rail_Splitter/         RailSplitter_Prototype_JLCPCB.zip, RailSplitter_FINAL_ASSEMBLY_BOM.csv
    kicad/Anish_Work/      RailSplitter_Electrical_Draft.kicad_pro (Rev C project)
                           AmplifierCircuit_MYCHANGES.kicad_sch (working changes)
  Howland_Pump/          HowlandPump_JLCPCB.zip, Howland_Read_Only_Calculation.json
    kicad/Anish_Work/      HowlandPump-AnishWork.kicad_pro (current, Oct 2)
                           older_versions/ (FINAL + Backout, Sep 30)
  Signal_Generator/      SignalGen_Prototype_RevC_JLCPCB.zip, FIRMWARE_COMPATIBILITY.txt
    kicad/Peter_Work/      SignalGen_Electrical_Draft.kicad_pro (Rev C project)
  Shared/                Boards not assigned to a single owner
    Channel_ADC_Interface_RevC/   kicad/Peter_Work/Channel_ADC_Interface_RevC.kicad_pro
    Channel_Monitor_C1/           kicad/Peter_Work/Channel_Monitor_C1.kicad_pro
    Monitor_Power_C1/             kicad/Peter_Work/Monitor_Power_C1.kicad_pro
    Shared_Power_RevC/            kicad/Peter_Work/Shared_Power_Adapter_RevC.kicad_pro
docs/
  Prototype_Demo_2.pdf              Prototype Demo 2 presentation
  Firmware.md                       Firmware guide (commands, protocol, pins, build)
  RevC_Release/                     System-level release documents:
                                    READ_ME_FIRST.txt, Integration_Guide.html,
                                    Wiring_Map.json, Release_Checks.json, SHA256SUMS.json,
                                    Existing_Power_Review, Legacy_Power_Assembly/
firmware/                         Peter Zhao
  ESP32_S3_Brainstim_FW/            ESP32-S3 main controller (ESP-IDF 5.4): BLE control,
                                    4-channel FSM, OLED, battery/ADC; web/ BLE test UI;
                                    mobile/ Capacitor Android app; tests/ host simulations
  RP2040_DDS_FW/                    DDS daughter card, now Pico 2 W + DAC8830 (Pico SDK
                                    2.2.0; folder name is historical): DMA-paced SPI DAC
                                    streaming, waveform tables, I2C slave
releases/
  StimPrototype_Prototype_Package_RevC.zip   Original Rev C archive, unmodified
```

The files from the release archive have been distributed into the folders above.
Paths inside `Integration_Guide.html`, `Release_Checks.json` and `SHA256SUMS.json`
refer to the original archive layout — use the zip in `releases/` when you need
those links and checksums to line up.

## BOARDS AND TEST STATUS (Prototype Demo 2)

| Board | Owner | Soldered | Bench result |
|-------|-------|----------|--------------|
| Buck-Boost RevB | Anish | Yes | **Pass** — 3.3036 V out (target 3.3 V). Battery not yet on hand. |
| Boost 79.7 V | Pyth | Yes | **Pass** — 12 V in, ~79.5 V out (scope average). |
| Rail Splitter | Anish | Yes | **Open** — could not bring input to full voltage; op amp possibly damaged. Split appeared even. |
| Howland Pump | Pyth (design) / Anish (schematic + PCB) | Yes | **Fail** — measured 96 V peak-to-peak; LTspice simulation works. Not yet debugged. |
| Signal Generator RevC | Peter | Yes | Fixes applied (below); expected ±3.3 V output. |
| Monitor Power C1 | Shared | — | New in Rev C — one local supply per channel. |
| Channel Monitor C1 | Shared | — | New in Rev C — external sensing/relay board. |
| Channel ADC Interface RevC | Shared | — | New in Rev C — isolated simultaneous I/V sampler per channel. |
| Shared Power RevC | Shared | — | New in Rev C — OPTIONAL regulated 5 V bench adapter (not a raw-cell supply). |

## DESIGN NOTES

**Buck-Boost (TPS63001).** Steps the 2.8–3.7 V battery up or down to a steady
3.3 V. The battery starts near 3.7 V and decays toward 2.8 V.

**Boost (LM5155).** 12 V in → 79.7 V out. Vout = Vin ÷ (1 − D), so the switch is on
about 85 % of the time.
- Feedback set to 79.7 V: R1 = 787 kΩ, R2 = 10 kΩ.
- Parts that see 80 V were up-rated: Q1 100 V → 150 V; D1 100 V → 200 V;
  top feedback resistor 0603 (~75 V max) → 1206 (200 V);
  C5 22 µF / 100 V → 100 µF / 160 V.

**Rail Splitter (OPA455).** Splits 79.7 V into HV_TOP = +39.85 V,
MID = 0 V, HV_BOTTOM = −39.85 V (relative to MID). MID is the reference for the
Howland and the isolated signal stage. Matched 100 kΩ 0.1 % divider, output
resistor for stability, added bulk and rail-to-rail filter capacitors, and a
+40 / MID / −40 connector to other boards.

**Howland Pump (OPA462).** Circuit designed by Pyth; schematic and PCB by Anish. Designed for up to ±5 mA. Rails are ±40 V
rather than ±30 V to avoid saturation near the voltage limit. The 2 kΩ Rload1
placeholder is DNP; use external 2–5 kΩ dummy loads. A read-only calculation in
the release flags a resistor-ratio mismatch; the proposed R11 20k → 24k
correction is **not** applied. Confirm fitted values before wiring.

**Signal Generator (Pico 2 W → ISOW7741 → DAC8830 → OPA2192).**
Vout = 2 × Vdac − 3.3 V, so DAC 0 / 1.65 / 3.3 V → −3.3 / 0 / +3.3 V.
- The DAC8830 output (~6.25 kΩ source) was driving a 10 kΩ load directly;
  the OPA2192 now buffers it.
- Footprint changed from the 6-pin DAC8411 to the 8-pin DAC8830.
- At power-up the DAC outputs code 0 = −3.3 V (full negative). **Firmware must set
  mid-scale first.**
- Firmware is now modular: TypeScript front-end web app, C back-end, Student and
  Teacher views.

## FIRMWARE

Written by Peter Zhao.

Full documentation: [`docs/Firmware.md`](docs/Firmware.md) — architecture, BLE
commands, I2C protocol, pin maps, safety sequencing, build/test and TODOs.

Two projects that must be updated together — they share the 15-byte GET_STATUS v2
I2C protocol. Detailed handoff: `firmware/ESP32_S3_Brainstim_FW/docs/phase1.md`.
Keep both folders side by side: the host tests find the RP2040 code at
`../RP2040_DDS_FW`.

- **RP2040:** `cmake -S . -B build-phase1 -G Ninja -DCMAKE_BUILD_TYPE=Release`, then
  `cmake --build build-phase1` (needs `PICO_SDK_PATH`, `PICO_TOOLCHAIN_PATH`).
- **ESP32-S3:** `idf.py build` with ESP-IDF 5.4.
- **Host tests:** `node tests/run_phase1.mjs` from `ESP32_S3_Brainstim_FW/`
  (needs Node and Clang with wasm-ld).
- Build output (`build/`, `build-*/`, `sdkconfig.old`, `node_modules/`) is
  git-ignored. Regenerate locally.

Not yet flashed or validated on hardware. The ESP32 refuses stimulation until HV
GPIO assignments and polarity are confirmed in `main/gpio_pins.h`.

## ORDER SETTINGS

2 layers; FR4; nominal 0.4 mm; 1 oz copper; ENIG; green mask; white silk;
tented vias. Follow the per-board README. These files do not order assembled
PCBs or purchase components. Support these thin boards during assembly.

## CHECKS

`Release_Checks.json` in the release archive records a fresh DRC (KiCad 10.0.6,
2026-10-17) on every packaged native project; reports are in `Release_Validation/`
and file hashes in `SHA256SUMS.json`. DRC/ERC validate file connectivity and
geometry, not powered performance.

## OPEN ISSUES

1. Howland: 96 V p-p on the bench despite a working simulation — investigate.
2. Rail Splitter: replace/verify the op amp and retest at full 79.7 V input.
3. `boards/Rail_Splitter/kicad/Anish_Work/AmplifierCircuit_MYCHANGES.kicad_sch` lists C1–C4 as
   **10 V** capacitors, but they sit on an 80 V / ±40 V rail (the presented design
   uses 100 V parts). Check which values were actually fitted.
4. Battery, charger and protection not yet selected; runtime unknown.
5. Do not parallel separate 3.3 V outputs. The two primary supply connectors on
   each SignalGen board are internally tied together.
6. Firmware/hardware DAC mismatch — **resolved 2026-10-30**: the next build targets
   the **DAC8830 on a Pico 2 W**, and `firmware/RP2040_DDS_FW` was retargeted
   accordingly (16-bit mode-0 frames, PIO CS framing, Rev C pin map, EN/FLT fault
   bit). Still open: compile with the Pico SDK, re-run the host tests, scope the
   DAC interface, and decide how the board's second channel is driven
   (`boards/Signal_Generator/FIRMWARE_COMPATIBILITY.txt`, `docs/Firmware.md`).
