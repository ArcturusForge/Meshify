# ADR-0001: Core technology stack for release one

Status: proposed, pending Stage 0 prototypes. Decided so far: GPL-3.0 licence, C++ as the main language, no recurring licence costs.

## Decision

| Area | Choice | Licence |
| --- | --- | --- |
| Language | C++20 | n/a |
| Interface | Qt 6 Widgets with Qt Advanced Docking System | LGPL |
| CAD kernel | Open CASCADE (OCCT) | LGPL 2.1 with exception |
| Sketch constraints | PlaneGCS | LGPL |
| Mesh booleans | Manifold | Apache-2.0 |
| Hollowing and offsets | OpenVDB | Apache-2.0 |
| Simplification and render clusters | meshoptimizer | MIT |
| Renderer backend | Qt RHI or Dawn, chosen by prototype P4 | LGPL / BSD-3 |
| Scripting plugins | Python | PSF |

Commercial kernels such as Parasolid and C3D are ruled out by the no-recurring-costs rule. GPL-3.0-compatible libraries, such as CGAL, are allowed because Meshify itself is GPL-3.0.

## Consequences

- Meshify must build its own stable-identity layer on top of OCCT; no library provides one. Prototype P1 tests this.
- OCCT sits behind Meshify's own geometry interface, so a later kernel change stays inside one layer.
- CI runs on Windows, macOS and Linux from the first commit.

## Stage 0 prototypes

| # | Prototype | Pass condition |
| --- | --- | --- |
| P1 | Token rebuild with stable references | GEO-01 passes on 20 scripted edits; broken references reported |
| P2 | OCCT stress set: fillets, shell, offset, booleans on text | At least 90% of 30 agreed cases succeed with usable errors |
| P3 | Qt shell with multi-monitor docking | UX-03 passes on all three platforms |
| P4 | Renderer: 5M-vertex model with local edits | Edits update in under 50 ms on reference hardware |
| P5 | Python plugin operation with undo, save and reopen | EXT-01 and HIS-01 pass |
| P6 | Supported STL through Blueprint Studio to a print | Supports survive slicing and print |
