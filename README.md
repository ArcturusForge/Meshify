# Meshify

Meshify is a planned desktop application for precise CAD modelling, organic mesh editing and 3D-print preparation, for Windows, macOS and Linux.

Implementation has not started. The project is in **Stage 0**: small prototypes that test the riskiest technical choices before production code is written.

## Direction

- C++20 core, Qt 6 interface, Open CASCADE (OCCT) as the precise-geometry kernel.
- Mesh work planned on Manifold, OpenVDB and meshoptimizer.
- Fully offline; real-world units throughout; no telemetry by default.

## Licence

Meshify is free software under the [GNU General Public License v3.0](LICENSE).

## Stage 0 prototypes

| # | Question | Status |
| --- | --- | --- |
| [P1](prototypes/p1-token-identity) | Do fillets and other later steps survive upstream edits on Open CASCADE? | Passed: 20 of 20 edits |
| [P2](prototypes/p2-occt-stress) | Does Open CASCADE handle 30 hard fillet, shell, offset, boolean and relief cases? | Passed: 27 of 27, all 3 bad inputs rejected |

The architecture decision record is in [docs/adr/0001-core-stack.md](docs/adr/0001-core-stack.md).
