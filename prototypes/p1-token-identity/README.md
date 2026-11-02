# P1: token rebuild with stable references

**Question:** can Meshify keep track of faces and edges through upstream edits on top of Open CASCADE, so later steps such as fillets survive or are reported clearly when they cannot?

This is the highest-risk item in ADR-0001: no free kernel solves this "topological naming" problem for us.

## Approach

- The token is rebuilt from a short history: sketch circle, extrude, rim recess, raised relief, fillet.
- Every face gets a logical name from the step that made it, such as `extrude/top` or `recess/wall`.
- Booleans and fillets pass names on through Open CASCADE's own history (`Modified`, `Generated`, `IsDeleted`).
- An edge is referenced by the names of the two faces it separates. The fillet stores three such references, not edge indices.
- Each step reports `ok`, `warning`, `needs input` (a reference has no match), `failed` (the kernel could not compute it) or `suppressed`.
- A failed or paused step never leaves a partial result, and every rebuilt version is kept so an earlier one can be restored.

## Result so far

| Check | Brief scenario | Result |
| --- | --- | --- |
| 20 scripted edits to diameter, thickness, rim, depth and relief | GEO-01 | 20 of 20 rebuild; all 3 fillet references match; face names identical |
| Recess suppressed | GEO-01 | Fillet paused: "1 of 3 edges could not be matched: [extrude/top \| recess/wall]" |
| Fillet radius 3 mm on a 0.8 mm recess | HIS-01 | Step fails; model is exactly the pre-fillet result |
| Relief wider than the recess | GEO-01 | Rebuilt with a warning |
| Restore the version from before an edit | GEO-01 | Restored without rebuilding |
| Volume without relief and fillet | Units | Matches the analytic value to 1e-6 |

## Known limits

- Face-pair references cover this token. Shapes where two faces meet along several separate edges will need an extra hint, such as edge order or position, to pick one of them.
- Primitive tool faces are named by orientation (top, bottom, side), which is enough for boxes and cylinders but not for general solids.
- No sketch constraint solver, text, or saving to disk yet; those belong to later prototypes.

## Build and run

Needs CMake 3.21+, a C++20 compiler and Open CASCADE 7.6 or newer.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/prototypes/p1-token-identity/p1_token token.stl
```

On Ubuntu, `apt install libocct-modeling-algorithms-dev libocct-data-exchange-dev libtbb-dev` provides Open CASCADE. CI uses the conda-forge `occt` package on all three platforms.
