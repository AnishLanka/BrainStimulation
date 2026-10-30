SIGNAL GENERATOR — BARE-PCB BENCH PROTOTYPE; POWERED PERFORMANCE UNVALIDATED
Revision DRAFT-B, 2026-10-17

STATUS
This KiCad 10 project now includes the corrected schematic and a fully routed PCB for bare-board bench prototyping. Read PCB_RELEASE_NOTES.txt for manufacturing checks, order settings and limitations. It is not a patient-use release. The input schematic and the already-ordered Howland board have not been changed. This folder contains local symbol and footprint libraries, the exported netlist, ERC results, a connectivity comparison, and SVG/PNG previews. Open SignalGen_Electrical_Draft.kicad_pro or the .kicad_sch file. Keep the local library files and tables alongside the project.

IMPLEMENTED CHANGES
1. Corrected the DAC output loading using the existing OPA2192 devices.
   Channel 1: U4.3 now senses ISO_3V3_CLEAN. U4.1 and U4.2 form the full-reference follower on VREF_BUF. U4.5 connects directly to U3.1 on DAC_OUT_UNIPOLAR. R3.1 connects to VREF_BUF; R3.2 and R6.1 join U4.6 on IN-_B; R6.2 connects to U4.7 on BIPOLAR_OUT. R3 changed from 10k to 20k, matching the nominal R6 value.
   Channel 2: the corresponding devices are U7, U5, R11 and R12, with the _2 net suffix. R11 changed from 10k to 20k.
   Removed obsolete midpoint/divider components R1, R2, R4, R5, R7, R8, R9, R10, C16 and C32. The nominal revised transfer is VOUT = 2*VDAC - VREF. This avoids driving the old 10k input resistor from the DAC's unbuffered output, whose typical output impedance is 6.25k ohms.
   The resistor family remains 1%; resistor-ratio error, DC accuracy and settling still require specification and validation. With r=Rfeedback/Rreference, the midpoint output at DAC code 32768 is VREF*(1-r)/2. Opposite 1% errors allow approximately -33.33mV to +32.67mV at VREF=3.3V, corresponding nominally to -66.67uA to +65.35uA through a balanced 2mA/V Howland stage. At the maximum 3.465V reference, the magnitude can reach 35mV / 70uA before other errors. Specify a precision matched resistor ratio, calibrate, and measure actual DC output before claiming a zero-current midpoint or zero-mean AC operation. Calibration/code limits do not replace independent current limiting.

2. Corrected the U3/U5 footprint assignments.
   DAC8830 D-package devices require 8-pin narrow SOIC. U3 and U5 now use the bundled standard SOIC-8_3.9x4.9mm_P1.27mm footprint. The original PCB assigned a 6-pin DAC8411 footprint, which was incompatible. Existing DAC symbol pin numbers were checked against the TI pinout.

3. Added ISOW supply-capacitor connections and draft footprint specifications.
   C41/C44: 22uF nominal, 16V, 1206, isolated RAW supply to the corresponding isolated ground. Choose an actual capacitor whose effective capacitance at operating voltage is at least 10uF; MPN is TBD.
   C42/C45: 10nF, 50V, 0402, on the isolated RAW supply, intended within 1mm of each ISOW VISOOUT pin 12.
   C43/C46: 10nF, 50V, 0402, on SYS_3V3, intended within 1mm of each ISOW VDD pin 9.
   Existing 1uF and input bulk capacitance is retained. Placement and routing are complete; PCB_Validation.json and PCB_RELEASE_NOTES.txt record the file checks. Real supply ripple and return-path behavior still require bench measurements. Voltage, dielectric and footprint properties are specifications, not approved capacitor purchases.

4. Implemented the existing fault interface.
   U1.8 -> FAULT_TO_PICO -> J1.07, with new R13 = 10k from SYS_3V3.
   U6.8 -> FAULT_TO_PICO_2 -> J2.07, with new R14 = 10k from SYS_3V3.
   Pin 8 is labeled EN_FLT, reflecting the ISOW multifunction enable/fault pin; it is not a dedicated fault-only output. The MCU must read the line without driving it high against a fault. If firmware uses it to disable the converter, use the correct open-drain behavior and follow the TI timing requirements. Disabling this isolated supply is not a validated output-mute system.
   Removed the now-obsolete no-connect markers on J1/J2 pin 7. Corrected ISOW pin names 6=GNDIO and 15=GISOIN.

5. DRAFT-B closes the original regulator-headroom problem.
   U2/U8 TLV70233 regulators and their local drawing connections are removed. New R15/R16 are 0-ohm 0603 links: pin 1 connects to that channel's ISO_3V3_RAW; pin 2 connects to its legacy ISO_3V3_CLEAN net. DAC VDD and VREF therefore come from the same regulated ISOW rail as the isolated digital outputs. The CLEAN name is retained only for connector/net compatibility; it does not imply extra regulation or ripple rejection.
   Existing C8/C9 and C36/C38 supply bypass capacitors remain, along with the local DAC supply/reference capacitors and added ISOW bulk/10nF capacitors. The populated 0-ohm link supplies no designed RC attenuation. Choose a standard 0603 jumper rated for the channel load, with resistance <=50 milliohms; exact purchasing MPN remains TBD in this draft.
   VSEL remains grounded for 3.3V operation, and VISOIN remains connected to VISOOUT. No independent IA-derived DAC rail and no primary 5V bus are introduced. Downstream PWR_FLAG symbols describe power conducted through the jumpers for ERC; they are not additional physical components.

PRESERVED INTERFACES
J1/J2: 01=unused VBUS_5V / VBUS_5V_2; 02=SYS_3V3; 03=SYS_GND; 04=shared SPI_SCLK_SYS; 05=shared SPI_MOSI_SYS; 06=separate SPI_CS_SYS / SPI_CS_SYS_2; 07=the corresponding EN/FLT line.
J3: 1=BIPOLAR_OUT, 2=ISO_3V3_CLEAN, 3=ISO_GND_ISOW.
J4: 1=BIPOLAR_OUT_2, 2=ISO_3V3_CLEAN_2, 3=ISO_GND_ISOW_2.
SYS_GND and the two channel grounds remain separate nets. This schematic separation does not verify PCB creepage, clearance or system isolation.

ASSEMBLY CHOICES AND POWERED VALIDATION STILL REQUIRED
- The original LDO-headroom defect is resolved by the direct regulated-rail connection. The ISOW 3.3V output range is 3.135-3.465V under its stated supply/load conditions, within DAC8830's 2.7-5.5V supply range. VREF and VDD are the same rail, satisfying VREF <= VDD. Check total isolated loading, including anything connected to J3/J4 pin 2, against the ISOW converter capability.
- SUPPLY ACCURACY / RIPPLE: this rail is not a precision reference. Its +/-5% range becomes approximately +/-5% full-scale gain uncertainty before resistor/op-amp errors. ISOW lists 14mV peak-to-peak typical ripple for the 3.3V-to-3.3V case at 60mA with 20uF || 10nF output loading; this is not a guaranteed bound or a measurement of this board. Measure the actual DAC reference ripple and output error. See POWER_RAIL_CHECK_DRAFT_B.txt for margins and limits.
- Digital logic uses the same source rail. At minimum 3.135V, ISOW VOH >=2.835V provides at least 435mV above the conservative 2.4V DAC input-high requirement. ISOW VOL <=0.3V is at least 300mV below the stricter 0.6V DAC input-low requirement. These are static bounds at the stated output-current conditions; check ringing, ground bounce and transient sequencing on the assembled board.
- The system 3.3V front end must be sized for all signal and Howland power modules. The user's separate 12V GAPTEC modules and their preloads are not integrated or approved by this signal-only draft.
- HARDWARE MUTE / CURRENT LIMIT: no independent startup inhibit or 5mA current limit has been added. The ordered Howland circuit, its current-setting resistor, rail arrangement and external interlock must be reviewed as a system before energizing outputs.
- CODE POLARITY: increasing DAC code now increases bipolar output. Nominal code 0 gives -VREF, code 32768 gives 0V, and code 65535 gives VREF*(1-2/65536). The DAC powers up at code 0, so this circuit starts with a nonzero command. Hardware must inhibit the output until rails are valid and firmware loads the intended zero-current code. Firmware alone is not an established independent inhibit.
- Existing IA0305S converters supply the op-amp rails. Their load-regulation specification is characterized over 20-100% load; the sheet does not state a mandatory minimum load, and says no-load operation may miss specifications. No arbitrary preload or clamp has been added. Measure both rails during startup, shutdown, idle and maximum signal load; verify amplifier/capacitor voltage ratings and unclipped output swing. A 200-ohm preload on each +/-5V rail would add 0.25W per module at nominal voltage, worsening the system power budget, and would still require worst-case resistor and rail checks.
- WAVEFORM PERFORMANCE: two independent waveform frequencies can use shared SPI with separate chip selects, but DAC8830 has no LDAC input for simultaneous channel updates. At 30kHz, 500ksps per channel gives about 16.7 samples/cycle; 1Msps gives about 33.3. Combined serial payload is 16Mbit/s or 32Mbit/s respectively, before chip-select gaps and timing margins. Isolation skew, 10ns DAC setup time, 30ns minimum CS high time, analog settling, load and firmware timing must be validated; no 30kHz waveform-quality guarantee is made.
- Final capacitor MPNs, effective capacitance, resistor-ratio accuracy and powered bench checks remain open. Placement, routing, connector geometry, geometric domain spacing and board DRC are now checked in PCB_Validation.json. Geometric spacing does not qualify medical isolation. Recovered source footprints other than the corrected DACs have not all been independently dimensioned against manufacturer drawings; confirm exact purchased variants before assembly.

VERIFICATION COMPLETED
The final routed board has zero DRC violations, zero unconnected items and zero schematic parity issues. FABRICATION_VERIFICATION.json and Fabrication_Independent_Audit.json record the Gerber, outline, drill and ZIP checks. Original connector geometry and outline are retained with a 1 nm import-rounding difference at J1. PCB_RELEASE_NOTES.txt specifies the bare-board release and ordering settings.
KiCad CLI 10.0.6 exported this schematic successfully. The final ERC_draft.json reports zero errors and zero warnings. The exported netlist matches 213 expected connected physical pins with zero differences; see connectivity_check.json. The comparison preserves source connections except the explicit changes above. Three ground domains remain separate. The SVG/PNG drawing was rendered and inspected. ERC does not validate measured rail behavior, analog accuracy, firmware, isolation safety or a physiological current limit.

PRIMARY DATASHEETS USED
DAC8830: https://www.ti.com/lit/gpn/dac8830
ISOW7741: https://www.ti.com/lit/ds/symlink/isow7741.pdf
TLV702: https://www.ti.com/lit/ds/symlink/tlv702.pdf
OPA2192: https://www.ti.com/lit/ds/symlink/opa2192.pdf
IA series: https://www.xppower.com/storage/portals/0/pdfs/SF_IA.pdf
