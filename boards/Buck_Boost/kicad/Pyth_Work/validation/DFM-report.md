# BuckBoost Rev B — independent geometric DFM verification

The final Rev B candidate passes the geometric checks below. This audit inspected both the editable PCB and the emitted fabrication Gerbers; it did not modify either. The manufacturer's online checker was not run by this audit.

PCB SHA-256: `254a79a8537299e081462e889d707ce6b0238f3eb09f3971d4ccd1216448959d`

## Results

| Check | Measured minimum | Audit criterion | Result |
|---|---:|---:|---|
| Visible silkscreen stroke | 0.160 mm | 0.150 mm | Pass |
| Visible text height | 1.000 mm | 1.000 mm | Pass |
| Silkscreen to component mask opening | 0.324 mm | 0.150 mm | Pass |
| Silkscreen to drilled hole | 0.297 mm | 0.150 mm | Pass |
| Via copper edge to component copper pad | 0.350 mm | No overlap | Pass |
| Via drilled-hole edge to component mask opening | 0.449 mm | No overlap | Pass |
| Via copper edge to component mask opening | 0.299 mm | No overlap | Pass |
| Separate component mask openings | 0.148 mm | 0.130 mm | Pass |
| Component mask opening to foreign-net copper | 0.150 mm | 0.090 mm | Pass |

Distances are geometric estimates rounded to 0.001 mm. The explicit no-overlap via checks also have ample measured positive separation; the values are included so a fabricator can apply its own process limits.

All 17 vias remain present. The design retains 27 numbered component pads and the 0.4112 mm total stackup. The top mask Gerber contains 27 component openings and the bottom contains four terminal-pad openings; the vias have no separate mask openings.

The minimum silk-to-via-annulus clearance is 0.146 mm between the C4 reference text and its ground via. This annulus is covered by soldermask, and the drilled hole is approximately 0.297 mm from silk. The stated silk-to-hole and silk-to-exposed-pad criteria pass.

## IC footprint dimensions

U1 pins 1–10 are each **0.250 x 0.600 mm**, with 0-degree pad rotation; these are also their actual X/Y dimensions on the board. Their width grew by 0.005 mm on each side relative to Rev A, leaving the centers, pitch and length unchanged.

The exposed pad, pin 11, is stored locally as **1.650 x 2.400 mm at 270 degrees**, so its actual board X/Y size is **2.400 x 1.650 mm**. The generated top-copper Gerber independently confirms 0.250 x 0.600 mm signal-pad and 2.400 x 1.650 mm exposed-pad apertures.

## Independent export cross-check

A second script directly parsed the emitted mask regions and silkscreen drawing commands. It measured:

- Mask web: 0.148437 mm.
- Silk to mask: 0.324034 mm.
- Via hole to mask: 0.449218 mm.
- Silk to via hole: 0.297790 mm.
- Top silkscreen aperture widths: 0.160 and 0.200 mm.

These agree with the native-PCB calculations to the geometric approximation tolerance. Foreign-net copper checks include tracks, pads, vias and the filled top ground plane. They exclude intentional connections between a pad and copper on its own net.

## Rev A findings resolved

The C4 via drill no longer overlaps its pad mask opening; the C2 via no longer overlaps its component copper pad; the four IC exposed-pad flank vias have greater separation. All 14 previously identified short silk-to-mask gaps are removed, undersized text/strokes are corrected, and J2 silk no longer touches stitching-via holes.

The five orange screenshot findings labeled "solder mask opening exposing trace" did not include locations. The final revision has no foreign-net copper overlapping its component mask openings. Traces intentionally connecting to their own pads extend into those pad openings, as expected. The original aggregate checker count cannot establish which features it meant.

## Method and evidence

KiCad 10.0.6 Python read the board without saving changes. Native polygons used a 0.001 mm maximum approximation error; distance searches resolved 0.0001 mm. The independent Gerber parser used the emitted polygon vertices and stroke widths, with fine segmentation of circular marks. The audit covers the geometries listed above; normal DRC/ERC, connectivity checks and physical power-converter testing remain separate validations.

Machine-readable evidence: `DFM-geometry.json`, `DFM-summary.json`, `DFM-Gerber-geometry.json`, `U1-pad-dimensions.json`. `DFM-geometry.json` includes the exact board and Gerber SHA-256 hashes used for this audit.

