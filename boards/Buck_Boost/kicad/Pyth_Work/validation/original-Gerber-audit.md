# Gerber fabrication audit

Inputs audited: `work/original/Updated Buck boost1` and its `PeterWork` subdirectory. These are exported fabrication files, not editable schematic/board source. All original files were left unchanged.

## Confirmed findings

1. **PeterWork contains six open IC connections.** Its top-copper fill leaves U1 pins 2, 4, 5, 6, 7, and 8 physically isolated from their assigned same-net zones. The pad-to-same-net-zone gap is 0.254 mm in each case (polygon calculation within approximately 0.000001 mm). Their nets are NetL1_2, NetL1_1, NetC1_1, and NetC4_1 respectively. No bottom-layer copper or vias at those SMD pad positions reconnects them. Evidence in `PeterWork/Updated Buck boost1-Top Layer.gbr`: pad flashes at lines 69, 75, 78, 81, 83, 85; matching zones begin at 1666, 2101, 1744, and 1968. An explicit route from each pad is required; merely refilling these original zones reproduces the opens. The root export connects these same pads.

2. **The root export has zero copper-to-board-edge clearance.** Its GND region includes the exact outline coordinates X119.037100, X177.965100, Y96.494600, Y113.512600 mm. The PeterWork GND zone is inset 0.5005 mm from the profile. The outline is one closed rectangle, 58.928 x 17.018 mm centerline dimensions; gbrjob dimensions include the 0.05 mm profile line width.

3. **Root copper violates its declared 0.2 mm spacing rule.** Polygon geometry finds approximately 0.1524 mm minimum copper spacing from GND to several nets, and approximately 0.1617 mm between NetL1_1 and NetC1_1. The exported gbrjob declares 0.2 mm outer copper spacing. PeterWork raises the measured minimum net-to-net spacing to approximately 0.20035 mm, but has the opens above.

4. **There is no bottom ground plane or ground/thermal via array.** Bottom copper contains the four connector pads, three NetC2_1/VOUT vias, and a 0.254 mm trace joining two VOUT vias. The PTH drill contains only 3 via drills of 0.711 mm and 4 connector drills of 1.5 mm. No ground vias are present under or adjacent to U1. This is a confirmed layout property; adequacy of thermal/current performance needs circuit operating limits and datasheet assessment.

5. **Two incompatible fabrication revisions are bundled.** After removing timestamp metadata, all corresponding manufacturing files match except top copper. Component placement, mask, paste, drill files, and outline are the same. Root top copper is 26,300 text bytes; PeterWork is 55,724. Neither set should be used indiscriminately as the final release.

6. **Stackup metadata needs deliberate review.** Both gbrjob files declare a 0.4112 mm total board (0.32 mm FR-4 core, 0.0356 mm copper each side, 0.01 mm mask each side) and finish `None`. This may be intentional but must not silently become a default 1.6 mm fabrication order.

## Connectivity and clearance method

A small Gerber parser handles the formats actually present: absolute metric straight-line regions, circular/rectangular/rounded-rectangular flashes, and round-width traces. Copper primitives were converted into KiCad SHAPE_POLY_SET geometry and united per net. Each resultant component was matched to its pad positions. Rounded features are approximated by 96-segment circles, while the six isolated U1 pads and their nearest zone boundaries are straight-edged and therefore do not depend on that approximation. Separate 0.01 mm raster overlap checks found no foreign-net copper overlaps in either top layer. This is an audit of plotted copper, not a complete editable-board DRC/ERC.

The root top layer is connected per net; VOUT has two top-layer components that are connected by the bottom trace and vias. The same VOUT arrangement is present in PeterWork. Other PeterWork splits are the six open IC pads listed above.

