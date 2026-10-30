# BuckBoost Rev B — JLCPCB DFM corrections

Use **BuckBoost_RevB_fabrication.zip** for the next JLCPCB check. Open **BuckBoost_RevB.kicad_pro** in KiCad 10.0 or newer to edit the project. This revision supersedes Rev A for fabrication.

The board retains the TPS63001DRCR 3.3 V converter, original outline and connector locations, and the requested **0.4112 mm design thickness**. The battery operating range supplied was 2.8–3.7 V, with a load above 200 mA. The exact maximum continuous and peak loads remain unspecified.

The later pasted four-channel requirements are assessed in [SYSTEM_REQUIREMENTS_REVIEW.md](SYSTEM_REQUIREMENTS_REVIEW.md). This non-isolated supply has not been validated for the complete four-channel system; its total load and battery power budget remain unknown.

## Corrections in this revision

- Moved C2 and C4 ground vias clear of their component pads and soldermask openings. Rev A's C4 via drill intersected a solder opening, and C2's via was too close.
- Moved four ground vias beside the IC farther from its exposed pad. All 17 ground vias remain tented, and the ground planes remain connected.
- Moved perimeter vias away from printed markings; 14 vias were repositioned in total.
- Removed cramped decorative silkscreen on U1, C1–C4 and R1. Component references remain visible; U1 has a relocated pin-1 marker. Fourteen measured silk-to-pad-mask clearances below 0.15 mm in Rev A are eliminated.
- Increased text to 1.0 mm and minimum silk strokes to 0.16 mm. Repositioned labels to clear pads, holes, other printing and board edges.
- Widened U1 pins 1–10 from 0.24 to **0.25 mm**, retaining 0.60 mm length and 0.50 mm pitch. This is a 0.005 mm extension on each side of TI's nominal land pattern to meet JLCPCB's published minimum SMD-pad dimension. The exposed pad is unchanged.
- Specified ENIG finish in the fabrication metadata. Added stricter local rules: 0.20 mm silk clearance, 1.0 mm text height, 0.15 mm text stroke and 0.09 mm mask-to-unrelated-copper clearance.

## Verification and remaining checker uncertainty

KiCad reports **0 DRC violations, 0 unconnected items, 0 schematic parity issues and 0 ERC violations**. Checks include the configured errors and warnings; no individual DRC exclusions were added. The recovered source circuit, schematic and PCB agree on all **27 component pins across 6 nets**. Outline and terminal pad/drill preservation are checked independently.

`validation/DFM-report.md` and `validation/DFM-geometry.json` contain an independent geometry audit. It checks silk width/height, silk-to-mask, silk-to-drill, via-to-pad/opening, mask webs and mask-to-foreign-net copper. Native SVG/PNG previews are included for visual inspection.

**These are local checks, not a JLCPCB server pass.** The supplied screenshot contained aggregate counts but no feature locations or ZIP identity. Its 14 silk-to-pad findings and six via-to-pad findings are consistent with Rev A geometry. The five orange “solder mask opening exposing trace” findings could not be individually located from that screenshot. No unrelated net is exposed inside a component's mask opening in the local audit. A connecting trace entering its own pad opening is intentional; mask expansion has not been removed just to hide such a warning. Rerun JLCPCB on the Rev B fabrication ZIP and inspect any remaining flagged locations before ordering.

## Circuit and parts

The earlier electrical repair is retained: explicit IC escape traces replace missing copper-pour connections; top and bottom ground planes provide return paths; feedback uses a separate branch to the output capacitor; EN and PS/SYNC remain connected to filtered VINA for always-enabled, forced-PWM operation.

**L1 must be Coilcraft XFL4020-222MEC, 2.2 uH**, using the supplied footprint. The original library metadata conflicted with the displayed value and described an unsuitable 10 uH, 500 mA part. Do not substitute the original L1 part. C1–C3 are Murata GRM21BR61E106KA73L, 10 uF/25 V/X5R; C4 is TDK C2012X7R2A104K125AA, 100 nF/100 V/X7R; R1 is Vishay CRCW0402100RFKED, 100 ohm. See BOM.csv for complete purchasing details.

J1 is input; J2 is output. **Pin 1 is positive and pin 2 is GND on both connectors.** Source references IN/OUT were renamed J1/J2. Connector positions, 5 mm pitch and 1.50 mm drills are preserved. U1's exposed pad uses four paste windows, approximately 61% area coverage; review the stencil process with the assembler.

## Manufacturing settings

| Item | Specification |
|---|---|
| Outline at Edge.Cuts centerline | 58.928 × 17.018 mm |
| Copper layers | 2 |
| Design thickness | 0.4112 mm, preserved |
| Source stackup | 0.32 mm FR-4 core, 0.0356 mm copper per side, 0.01 mm mask per side |
| Copper weight | Approximately 1 oz per side |
| Finish | ENIG |
| Via diameter / drill | 0.60 / 0.30 mm; tented both sides |
| Holes | 17 vias and 4 terminal holes, all plated |
| Copper clearance / copper to edge | 0.20 / 0.50 mm |
| Mask expansion | 0.0508 mm on U1; other pad overrides preserved |
| Minimum U1 mask web | Approximately 0.1484 mm |
| Coordinate origin | Lower-left outline corner, shared by Gerbers, drills and placement |

JLCPCB lists 0.4 mm as its nominal thin-board option and ±0.1 mm thickness tolerance below 1 mm. The editable design remains 0.4112 mm; do not substitute a thicker board. Confirm the supplied stackup/tolerance meets the product fit. ENIG is specified because JLCPCB does not support HASL on boards at or below 0.4 mm. The thin board and heavy terminal blocks need appropriate mechanical support.

Use the complete Rev B fabrication set together. Do not mix it with previous exports. The empty NPTH file is intentional. The fabrication ZIP contains manufacturing layers, job metadata and drill files; drill maps are in the full project under fabrication/. No filled or plated-over via-in-pad process is requested. BOM and placement are generic assembly data; assembly-service-specific ordering codes, rotations and panel requirements still require the assembler's review.

## Prototype validation

This revision has not been physically tested. Inspect polarity, solder bridges, exposed-pad soldering and L1 before applying power. Check for input/output shorts, then use a current-limited bench supply and a controlled dummy load. Confirm 3.3 V at no load and above 200 mA; 250 mA is a starting test point, not a guaranteed maximum rating. Sweep input from 3.7 to 2.8 V, including startup at both ends, and measure regulation, ripple and temperature. Repeat with the actual system's continuous and pulsed load before connecting it.

The complete system must provide suitable battery protection/cutoff. No physical load, thermal, EMI or system-level safety validation was performed. Prior measurements on the old board do not validate this revision.

## Contents and references

- Editable PCB/schematic/project, portable local symbol/footprint libraries.
- BOM.csv, placement.csv, fabrication layers and native previews.
- validation/: electrical checks, source comparison, independent DFM measurements and via-move record.
- SHA256SUMS.txt: file-integrity hashes.

References checked for this revision: [JLCPCB fabrication capabilities](https://jlcpcb.com/capabilities/pcb-), [TI TPS63001 datasheet](https://www.ti.com/lit/ds/symlink/tps63001.pdf), [TI evaluation module](https://www.ti.com/lit/ug/slvu156/slvu156.pdf), [Coilcraft XFL4020](https://www.coilcraft.com/getmedia/50632d43-da1b-4cdb-8ab4-3029cab51df3/xfl4020.pdf). Component-specific links are included in BOM.csv.
