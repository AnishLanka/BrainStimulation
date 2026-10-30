MONITOR POWER C1 - CHECKED BARE-PCB BENCH PROTOTYPE

Use Monitor_Power_C1_Prototype_JLCPCB.zip from the outputs directory for this board only. Build four identical boards for four independently floating channels. The archive contains seven Gerber layers, two Excellon drill files and the Gerber job file at its root. It does not include parts or assembly service.

Order settings
2 layers, FR-4, nominal 0.4 mm, 1 oz outer copper, 100.00 x 60.00 mm outline, green solder mask and white legend. Select ENIG explicitly in the order form; the Gerber job leaves finish unspecified. Native stack thickness is 0.41116 mm and the job rounds it to 0.4112 mm. The 0.05 mm outline stroke makes the job bounding box read 100.05 x 60.05 mm; the closed profile centerline is exactly 100 x 60 mm. Use the closed profile for milling. No controlled impedance or stencil is required. The four 3.2 mm mounting holes are nonplated.

Electrical interface
J201 pin 1 = regulated system +5 V; pin 2 = SYS_GND. The connector is JST B2P-VH, mate VHR-2N with SVH-41T-P1.1 contacts.
J202 pin 1 = channel +5 V; pin 2 = this channel MID; pin 3 = channel -5 V; pin 4 = channel +3.3 V. The connector is JST B4P-VH, mate VHR-4N with the same contact family. The monitor-core J1 has the same numbered electrical order but a different connector family. Use a mapped harness and verify continuity by pin numbers, not connector appearance.
SYS_GND stays on the primary side. MID connects only to this channel's rail-splitter midpoint and isolated signal reference. Neither MID nor -5 V connects to the HV-bottom return. Keep all four channel MIDs separate. Do not parallel any output with IA0305S, ISOW7741, the Pico's own 3.3 V regulator or another regulator.

Component and assembly checks
PS201 is TMR 3-0511, not a TMR 1 or a dual-output part. Pins 1/2 are input -/+, pin 3 is open for ON, pin 4 is absent, pins 5/8 are NC, pin 6 is +5 and pin 7 is MID. The local footprint follows the official April 15, 2026 drawing: 21.8 x 9.1 mm body, 11.2 mm height, 2.54 mm pin pitch, 1.0 mm drills. Do not fit an alternate family without checking its pinout and dimensions.
PS202 is TMR 1-0511. Pins 1/2 are input -/+, pin 4 is MID and pin 6 is -5 V. Pins 3/5 are absent. Body is 17 x 7.62 x 11 mm. Its footprint differs from PS201.
F201 is 2 A slow 0454002.MR; F202 is 500 mA slow 0454.500MR. They share compatible SMD land geometry but must not be interchanged.
C201-C205 are polarized 100 uF/16 V EEUFR1C101, D5 x H11 mm, 2 mm lead spacing. C204 positive lead connects to MID and its negative lead to -5 V. Other capacitor polarity follows the plus marking.
U201 MCP1702-3302E/TO has pins 1 GND, 2 VIN, 3 VOUT. This board uses a wide TO-92 footprint: form the two outer leads to 2.54 mm spacing while supporting the body. Do not use an assumed generic TO-92 pinout.
R201 is 150 ohm and R202 220 ohm, each 1%, 0.6 W. Populate both; they establish the specified module load-regulation test region. C206-C210 are 100 nF/50 V X7R 0805 bypass capacitors.
The thin board requires mechanical support. Use insulating standoffs and support connectors while plugging/unplugging; do not flex the PCB with the tall modules mounted.

Power allocation and qualification
Per channel: allocate 350 mA from +5 V, 60 mA from -5 V and no more than 30 mA total from this board's +3.3 V. The 3.3 V current is included in the positive allocation. The +5 V module rating is 600 mA and the -5 V rating is 200 mA; the complete system's budget does not authorize loading every output to its rating simultaneously.
The +5 V allocation includes 100 mA for the local Pico and digital circuitry, 45 mA for ADC/analogue drivers, relay and monitor loads, preload and reserve. Pico consumption depends on firmware and connected I/O; measure it. Four boards request a 4 A AUX allocation from the system's regulated 5 V source. The estimated continuous input is 3.07 A at 5 V, using an explicit 60% planning efficiency and extra typical idle allowance. This is not a guaranteed worst-case inrush specification.
Keep at least 4.5 V at each module's actual input pins after fuse and wire drops. There is no active rail sequencing or fast discharge on this board. Preloads discharge local rail capacitors. Test startup, power-down, eight-module inrush, fuse coordination, ripple, load steps and case temperature using dummy loads before connecting the monitor and ADC. Confirm +3.3 V stays within supervisor/logic limits. Do not bypass a latched undervoltage fault to hide supply sag.
The two converters provide functional isolation. This PCB, the 3 mm copper separation rule and the component isolation-test ratings do not establish a patient-use insulation or leakage qualification.

Completed checks
KiCad 10.0.6: 19 electrical components, 45 connected pins, 3 intentional unconnected pins. ERC: 0 findings and 0 ignored checks. Final saved-zone DRC: 0 findings, 0 unconnected items, 0 schematic parity issues and 0 ignored checks. Local footprint pad coverage and native pin nets match the circuit source. The final zone fill is saved using this project's primary/channel separation rule.
Fabrication_Verification.json checks the closed profile, all 40 PTH and 4 NPTH drill coordinates/diameters, common origin, two copper layers, stack thickness and all ten ZIP entries against the generated files. Fabrication_Manifest.json records exact SHA-256 hashes. These are file and geometry checks; no circuit performance has been measured.
Independent_Geometry_Summary.json reports zero findings for both silkscreen layers against the expanded solder-mask openings, drill holes and annuli. Minimum silk-to-expanded-mask clearance is 0.21875 mm. Fresh_Export_Parity.json confirms all ten manufacturing files match an independent fresh export after creation timestamps are removed.

Primary references
TRACO TMR 3: https://www.tracopower.com/products/tmr3.pdf
TRACO TMR 1: https://www.tracopower.com/tmr1-datasheet
Microchip MCP1702: https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP1702-Data-Sheet-DS20002008.pdf
Panasonic capacitor: https://industrial.panasonic.com/ww/products/pt/aluminum-cap-lead/models/EEUFR1C101
Vishay resistors: https://www.vishay.com/docs/28724/mrs16m25.pdf
Littelfuse fuses: https://www.littelfuse.com/~/media/electronics/datasheets/fuses/littelfuse_fuse_452_454_datasheet.pdf.pdf
JST VH: https://www.jst.com/wp-content/uploads/2025/06/eVH.pdf
Wurth bypass capacitor: https://www.we-online.com/components/products/datasheet/885012207098.pdf
JLCPCB current capabilities: https://jlcpcb.com/capabilities/pcb-capabilities/
