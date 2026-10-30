FIRST PROTOTYPE PCB PACKAGE — REV C

Open StimPrototype_Hardware_Revision_C/Integration_Guide.html first.
Upload each individual manufacturing ZIP as a separate bare PCB design.
Do not upload this complete engineering archive as one PCB.

NEW BOARDS
- Monitor_Power_C1_Prototype_JLCPCB.zip: one local supply per channel.
- Channel_Monitor_C1_Prototype_JLCPCB.zip: external sensing/relay board.
- StimPrototype_Hardware_Revision_C/adc_interface/Channel_ADC_Interface_RevC_JLCPCB.zip:
  one isolated simultaneous current/voltage sampler per channel.
- Shared_Power_RevC_Prototype_JLCPCB.zip: OPTIONAL regulated5V bench adapter.
  It replaces the TPS63001/GAPTEC front-end branches; it is not a raw-cell supply.

EXISTING RELEASES INCLUDED UNCHANGED
BuckBoost RevB, Boost79V7, RailSplitter and SignalGen RevC bare-board ZIPs.
The original four-branch battery option remains a candidate needing measured
per-channel load/headroom and a separately budgeted SignalGen/monitor supply.
Do not parallel separate3.3V outputs. The two primary supply connectors on
each SignalGen board are internally tied together.

ORDER SETTINGS
2 layers; FR4; nominal0.4mm (native0.41116mm);1oz copper; ENIG; GREEN mask;
white silk; tented vias. Follow the per-board README. These files do not
order assembled PCBs or purchase components. Support these thin boards.

NATIVE PROJECTS AND CHECKS
Full native projects, local footprint/symbol libraries, rules, schematics,
BOMs and reports are included. Open .kicad_pro with all adjacent files.
Release_Checks.json records a fresh DRC on every packaged native project.
The four new boards have no ignored DRC categories or exclusions. Older
projects retain their previously reported default ignored categories.
DRC/ERC validate file connectivity/geometry, not powered performance.
Legacy_Power_Assembly contains exact procurement supplements for the
unchanged boost and splitter, resolving the earlier purchasing placeholders.
The ADC's former REVIEW_ONLY filename is renamed in this portable project;
PCB bytes are unchanged and fresh schematic parity passes.

USER'S RETAINED DESIGN / ASSEMBLY INTENT
Anish Howland and completed SignalGen remain unchanged. The Howland2k
Rload1 placeholder is DNP. Use external2k–5k dummy loads. No Howland rework
is included. A read-only calculation identifies a resistor-ratio mismatch;
the proposed20k→24k R11 correction was discussed but not applied here.
Confirm actual fitted values and ordered output-terminal access before wiring.

WHAT THIS BUILD IS FOR
Evaluate nominal79.7V boost output, approximately±39.85V rails aboutMID,
firmware-driven waveforms and monitoring. Frequency target remains0.5Hz–40kHz;
30kHz generation needs correctDAC8830 timing and sufficient sample rate.
The user handles firmware. Battery/charger/protection selection and runtime
are unresolved. First measure one channel's startup, regulation, current
accuracy over2k–5k, waveform quality, temperatures, and fault responses.
This is a dummy-load bench prototype, not a human-use release.
No order, firmware flash or powered test was performed by this work.
