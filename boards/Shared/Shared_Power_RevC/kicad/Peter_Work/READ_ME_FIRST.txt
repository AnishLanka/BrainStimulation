SHARED POWER ADAPTER REV C — BARE-BOARD BENCH PROTOTYPE

This is the routed, checked shared power PCB, not a completed battery system.
Upload Shared_Power_RevC_Prototype_JLCPCB.zip for this board. Open the complete
Shared_Power_Adapter_RevC.kicad_pro project beside its local libraries and .dru.
Fresh ERC, saved-fill DRC, refilled DRC, connectivity and schematic parity pass.
No DRC exclusions or ignored checks are used. PCB_Validation.json and the
fabrication manifest identify the exact checked files.

ORDER SETTINGS
2 copper layers, 105 x 90 mm, nominal0.4 mm FR4, 1oz copper, ENIG, green mask,
white silkscreen, through vias tented both sides. Native thickness0.41116 mm
retains the original thin-board intent. Minimum drill0.30 mm. The ZIP contains
seven manufacturing layers plus the plated drill file, at a common origin.
No blind/buried vias, plated slots or impedance-controlled traces are requested.
Use a rigid support under the thin PCB for connector insertion and module mass.

POWER AND CONNECTORS
Use a regulated nominal5V /8A current-limited external source. Never exceed
5.25V at J1. Use16AWG input wiring and the correct JST VH mating parts:
VHR-2N housing, SVH-41T-P1.1 contacts; board header is B2P-VH. The 8A source
allocation is not an unmeasured thermal rating of every possible load combination.

J1: pin1=regulated5V input, pin2=SYS_GND.
J2: pin1=3.3V bankA, pin2=SYS_GND; feeds one dual-channel SignalGen board.
J3: pin1=3.3V bankB, pin2=SYS_GND; feeds the other dual-channel SignalGen board.
J4/J5/J6/J7: channels1/2/3/4; pin1=isolated12V, pin2=that channel's HV_BOTTOM.
J8: pin1=AUX5V, pin2=SYS_GND;4A total allocation for all four monitor-power boards.
Pin1 is the square pad and front triangle. Back legends state the functions.
The two3.3V outputs must remain separate; never parallel their outputs.

Each R3M branch replaces the old GAPTEC module AND its200-ohm preload.
Its12V return goes to boost input return/HV_BOTTOM, not to the channel MID node.
Keep each channel's isolated return separate from SYS_GND and other channels.
The downstream rail splitter still supplies the intended nominal+/-40V rails.
Do not parallel this supply with the original buck-boost or module outputs.

COMPONENTS AND ASSEMBLY
Candidate_BOM.csv contains the selected MPNs and source links. TPS63802DLAR,
Coilcraft XFL4015-471MEC and RECOM R3M-1212S/SMD use verified manufacturer lands.
The selected280k/49.9k0.1% divider gives3.30561V nominal. This replaces the early
511k TNPW0603 candidate, whose resistance exceeded that series' published range.
10xMurata GRM31CR61C226KE15L are22uF16V1206 ceramics. Input effectiveC must
remain>=5uF and each pair of output capacitors>=14uF over bias/temperature.
Assembly_Placement_Top.csv is a placement reference, not a submitted PCBA order.
The factory/assembler must verify its own rotations against pad1 in the native PCB.

WHAT REMAINS TO MEASURE
With dummy loads, measure startup/inrush, fuse coordination,3.3V ripple/load
steps,12V regulation and temperatures before attaching the remaining boards.
R3M and TMR inputs must remain>=4.5V at their module pins after all cable/fuse
drops.5.0V at the external source alone does not establish that condition.
The budget estimates5.53A at5V for a conservative four-channel scenario,
including3.07A monitor allocation; actual firmware and load behavior can differ.
All branch maxima simultaneously exceed the8A source allocation.
No battery, charger, cell protection or measured one-hour runtime is included.
The3.5mm PCB domain rule and module test-voltage rating do not establish a
patient-connected isolation rating. This release is for the bench prototype.
