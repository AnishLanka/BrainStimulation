CHANNEL MONITOR C1 — BARE-BOARD BENCH PROTOTYPE

This separate monitor does not modify Anish's Howland PCB or SignalGen Rev C.
The Howland schematic's 2k test-load placeholder will be unpopulated, as the
user specified. Use external 2k–5k dummy loads. The actual ordered Howland
copper/output terminal has not been supplied; no physical pad location or
connector mating compatibility is assumed here.

FABRICATION
Upload Channel_Monitor_C1_Prototype_JLCPCB.zip as one bare PCB design.
180 x 145 mm, 2 layers, nominal 0.4 mm FR4, 1 oz copper, ENIG, green mask,
white silk, vias tented on both sides. Native thickness is 0.41116 mm.
Use the four M2.5 supports on this thin board; avoid connector/relay strain.
Green mask is specified because the smallest measured mask web is 0.100 mm.
This is a large modular bench prototype, not a phone-size final assembly.
The BOM and placement files are assembly references, not a placed PCBA order.
Open Channel_Monitor_C1.kicad_pro with all bundled libraries and rule files.

VERIFICATION
Fresh ERC and stored/refilled DRC: zero findings, unconnected items and
schematic parity issues. No ignored checks or DRC exclusions. All 394
connected pins and 10 intentional NC pins match source, exported XML and PCB.
PCB_Validation.json and Fabrication_Manifest.json identify the checked files.
Native and emitted fabrication geometry are checked separately. File checks
do not establish analog response, startup behavior or patient isolation.

CONNECTIONS — ALL MID CONNECTIONS BELOW BELONG TO THIS CHANNEL ONLY
J1 LOCAL POWER: 1=+5 V, 2=MID, 3=-5 V, 4=+3.3 V.
  Use one Monitor_Power_C1 output per channel. These are not the HV rails.
J2 HOWLAND INPUT: 1=existing Howland output node, 2=its MID reference.
  The Howland schematic J2 is the command input, NOT this output connection.
  Verify an existing accessible output terminal before connecting a harness.
J101 LOAD: 1=external positive terminal, 2=external return terminal.
  Both wires pass through normally open relay contacts. Do not bypass the
  relay or shunt with another MID/ground return from the dummy load.
J3 CONTROL: 1=MID, 2=RUN_REQUEST, 3=ARM_EDGE, 4=HEARTBEAT,
  5=raw FAULT_N, 6=INHIBITED. Logic level is local 3.3 V.
J4 OPTIONAL ENABLE: 1=OPA_ENABLE, 2=MID. LEAVE OPEN for the unchanged
  Howland assembly unless an existing documented compatible interface exists.
  No pull-down addition or enable-pad rework on Howland is required by this
  handoff. With J4 open, the relay disconnects the external load while the
  Howland's internal output can remain active.
J5 ADC SIGNALS: 1=MID, 2=I_MON, 3=V_MON, 4=REF_4V096,
  5=+5 V, 6=+3.3 V, 7=raw FAULT_N. The ADC uses its own internal references;
  J5.4 is a reference test signal, not an ADC reference supply connection.
J6 CONDITIONED FAULT: 1=FAULT_CLEAR_N, 2=MID, 3=+3.3 V.
  Connect the ADC controller's conditioned-fault input to J6.1.
  Do not substitute raw FAULT_N from J3.5 or J5.7.
J7 HV MONITOR: 1=HV_TOP, 2=MID, 3=HV_BOTTOM (nominal +40/0/-40 V).
J8 COMMAND MONITOR: 1=existing bipolar command, 2=MID. It observes the
  same command that feeds Howland; it does not alter the signal board.
TP1=I_MON; TP2=V_MON; TP3=raw FAULT_N; TP4=2.048 V bias.

The headers are unpolarized. Verify pin 1 and every cable conductor from the
native schematic and Wiring_Map.json. J6 is a sense/control interface,
not the former external-actuator connection described in earlier drafts.

NOMINAL FUNCTIONS AND LIMITS
The 100 ohm return shunt adds 0.5 V at 5 mA. Its Kelvin sense connections are
kept separate from the load-current path. I_MON=2.048+324*I_A volts.
V_MON=2.048+V_port/20 volts; port voltage is differential across the external
branch's internal relay terminals. Nominal gross-fault windows are about
plus/minus5.696 mA and plus/minus28.516 V. They are trip thresholds, not
instantaneous hard limits: tolerances, bandwidth and relay delay matter.
The Howland supplies nominal current; no additional precision5mA limiter
was added. There is no independent DC-charge limit or DC-blocking capacitor.
This preserves low-frequency observation down to the requested0.5 Hz.
40 kHz amplitude/phase response and fault response require measurements.

The controller must hold ARM_EDGE low during startup/faults, initialize the
DAC command near zero, and wait for valid supply/reference/HV windows.
RUN_REQUEST low requests inhibit. A heartbeat is required when enabled.
The watchdog's nominal timeout is200 ms (170–230 ms design range).
The command must remain in its near-zero window for at least420 ms before
arming; apply an ARM rising edge only after qualification is stable.
Fault assertion clears the latch; recovery alone does not rearm it.
The relay is normally open. The optional amplifier-enable output has a
12–28 ms delay after relay energizing, but is unconnected in this assembly.
Firmware must also implement the user's normal amplitude ramp; emergency
fault opening is a different response. A disconnected command biases away
from the arm window. Verify all behavior with dummy loads before relying on it.

FIRST BENCH MEASUREMENTS
Check rails and reference before fitting cables to other boards. Check relay
open/closed continuity, fault latch, reset and watchdog with dummy signals.
Then test current and voltage sensing at DC/0.5 Hz and across30–40 kHz;
measure phase/gain, clipping, startup, brownouts and shutdown transients.
The unchanged Howland resistor-ratio calculation predicts load-dependent
current; verify it over2k–5k before any separately reviewed component change.
Shared grounded instruments/USB must not bridge channel MID domains. Use
appropriate isolated/differential measurements. This release is for dummy-load
bench development, not a human-connected stimulation assembly.
