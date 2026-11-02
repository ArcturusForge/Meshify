# P2: Open CASCADE stress test

Runs 30 hard modelling cases through Open CASCADE (OCCT) and records, for each one, whether it gives a valid solid, whether it fails safely, and how long it takes. This answers the ADR-0001 question of whether OCCT is good enough for Meshify's fillet, shell, offset, boolean and relief work.

**Pass condition:** at least 90% of the 27 cases expected to work give a valid solid (checked with `BRepCheck_Analyzer` and a positive volume), and all 3 cases that should fail are rejected rather than reaching the model.

## Result: PASS

- 27 of 27 cases expected to work gave a valid solid.
- 3 of 3 bad inputs were rejected.
- Total run time was about 2.7 s on a 4-core 2.1 GHz Xeon cloud VM (Release build, OCCT 7.6.3). Of that, 2.0 s was one case, #3.

## What this means for Meshify

1. **Validate after every operation.** In case #3, a fillet radius bigger than half the box, OCCT did not report an error. It returned a broken solid, and only the validity check caught it. So every feature step must run `BRepCheck_Analyzer` before its result is accepted. The other 29 cases suggest this check is cheap, and it backs the "failed step leaves no partial result" rule from P1.
2. **Merge coplanar faces after booleans.** Shelling the L-bracket (#18) failed until the faces split by the fuse were merged with `ShapeUpgrade_UnifySameDomain`. That clean-up belongs in the boolean step itself, not in each command that follows it.
3. **Bad inputs can be slow to fail.** Case #3 took about 2 s before giving up, against 1–180 ms for everything else. Feature rebuilds should run off the UI thread and be cancellable. Long term, a quick radius check before calling OCCT would help.
4. **Many-hole cuts are fine.** Cutting 49 holes in one boolean took 0.2 s, which is acceptable for the hole patterns on prints.

Two cases double as correctness checks. Case #1 (r 1 mm on all 12 edges) and case #9 (r 2 mm on one corner's three edges) give the same volume, 975.587 mm³. That is correct: case #9 is case #1 scaled by 2, with one corner instead of eight, and 2³ = 8.

## Results

| # | Group | Case | Result | Volume (mm^3) | Time (ms) |
| --- | --- | --- | --- | --- | --- |
| 1 | Fillet | Box, all 12 edges, r 1 mm | valid solid | 975.587 | 18.8 |
| 2 | Fillet | Box, all edges, r 4.9 mm (near half the size) | valid solid | 539.249 | 19.7 |
| 3 | Fillet | Box, all edges, r 6 mm (larger than half) | rejected by validity check (OCCT returned an invalid solid) | 1018.313 | 1852.1 |
| 4 | Fillet | Cylinder top edge, r 2 mm | valid solid | 3090.067 | 1.6 |
| 5 | Fillet | Token outer and inner rim edges, r 0.5 mm | valid solid | 4281.035 | 9.7 |
| 6 | Fillet | Token inner rim, r 3 mm on a 0.8 mm recess | failed cleanly: fillet not done | 0.000 | 8.8 |
| 7 | Fillet | Bracket concave inner edge, r 3 mm | valid solid | 14038.628 | 17.0 |
| 8 | Fillet | Tee of two cylinders, intersection edge, r 1 mm | valid solid | 3410.715 | 147.8 |
| 9 | Fillet | Box corner, three edges meeting, r 2 mm | valid solid | 975.587 | 5.0 |
| 10 | Fillet | Slot edges after a cut, r 0.5 mm | valid solid | 4997.854 | 16.4 |
| 11 | Chamfer | Box, all edges, 1 mm | valid solid | 945.333 | 10.5 |
| 12 | Chamfer | Cylinder top edge, 1.5 mm | valid solid | 3074.441 | 1.1 |
| 13 | Chamfer | Bracket concave inner edge, 2 mm | valid solid | 14040.000 | 16.2 |
| 14 | Chamfer | Token outer top edge, 0.6 mm | valid solid | 4277.819 | 6.9 |
| 15 | Shell | Box, open top, 1 mm wall | valid solid | 1844.000 | 10.4 |
| 16 | Shell | Cylinder, open top, 1.2 mm wall | valid solid | 1709.429 | 3.2 |
| 17 | Shell | Filleted box, open top, 1 mm wall | valid solid | 1674.740 | 19.7 |
| 18 | Shell | Bracket, open top, 2 mm wall | valid solid | 7458.265 | 34.2 |
| 19 | Shell | Box, 11 mm wall on a 20 mm box | failed cleanly: shell not done | 0.000 | 8.2 |
| 20 | Offset | Box outward 1 mm, rounded corners | valid solid | 1698.437 | 13.9 |
| 21 | Offset | Filleted box inward 0.5 mm | valid solid | 723.373 | 16.1 |
| 22 | Offset | Torus outward 0.5 mm | valid solid | 2418.053 | 0.4 |
| 23 | Boolean | Cylinder through box | valid solid | 3497.345 | 8.4 |
| 24 | Boolean | Two boxes sharing a face (coplanar fuse) | valid solid | 2000.000 | 6.0 |
| 25 | Boolean | Two tangent cylinders fused | valid solid | 1570.796 | 11.7 |
| 26 | Boolean | Plate with 49 holes in one cut | valid solid | 43072.788 | 195.1 |
| 27 | Boolean | Sphere and box intersection | valid solid | 959.233 | 54.6 |
| 28 | Boolean | Revolved goblet cut by a box | valid solid | 311.018 | 19.2 |
| 29 | Relief | Vector "+1" raised 0.6 mm on the token | valid solid | 4345.437 | 81.9 |
| 30 | Relief | Vector "+1" engraved 0.5 mm into a box | valid solid | 3562.312 | 56.3 |

Times come from a single run and vary by about ±20% between runs. They are only meant to show orders of magnitude. The figures for the Windows reference machine will come from CI and from Pixel's own run.

## Build and run

From the repo root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/prototypes/p2-occt-stress/p2_occt_stress   # prints the table; exit code 0 = PASS
```

`ctest` runs it as well.
