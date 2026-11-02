# Meshify: notes for Claude

Read this first instead of exploring the repo. Keep sessions cheap: read only the files a task needs, and don't re-derive what is written here.

## What this is

Desktop CAD + mesh + 3D-print-prep app. C++20, Qt 6 (later), Open CASCADE (OCCT) kernel. GPL-3.0.
Currently **Stage 0**: small prototypes under `prototypes/`, each answering one question from `docs/adr/0001-core-stack.md`. No production code yet.

## Layout

- `CMakeLists.txt`: top level; one `add_subdirectory` per prototype.
- `prototypes/pN-<name>/`: `src/`, `tests/`, `CMakeLists.txt`, `README.md` (question, approach, results table, known limits).
- `docs/adr/`: architecture decisions. Don't reread the ADR unless the task is about architecture.
- `.github/workflows/`: `build.yml` (reusable build on Linux, macOS, Windows, conda-forge `occt=7.8`, ccache), `ci.yml` (PRs and main), `release.yml` (tag `vX.Y.Z`).
- `cmake/MeshifyPackaging.cmake`: install/CPack rules; register shipped binaries with `meshify_install(<target>)`.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                      # incremental; never delete build/ unless configure is broken
ctest --test-dir build --output-on-failure
```

Build one target when that is all you changed: `cmake --build build --target p1_token_tests`.
OCCT on Ubuntu: `apt install libocct-modeling-algorithms-dev libocct-data-exchange-dev libtbb-dev`.
OCCT 7.8 moved the STL writer to `TKDESTL`; CMake already handles both.

## Releases

Push a tag `vX.Y.Z` (or `vX.Y.Z-alpha.N`) on main: `release.yml` builds all three platforms, smoke-tests the archives and publishes the GitHub Release. 0.x and suffixed versions are pre-releases. `cpack` in `build/` makes the local archive.

## Conventions

- One prototype per PR, small and focused. Each prototype README gets a results table with timings.
- Tests are plain executables registered with `add_test`; no test framework yet.
- Real-world units (mm) throughout. No telemetry.
- Windows: `windows.h` defines `near`/`far`/`min`/`max`; never use them as names.

## Working cheaply

- Don't run `find`/`grep` over the whole repo for things listed above.
- Pipe long build or test output through `tail -n 40`; only show full logs when something fails.
- After pushing a PR, don't poll CI. End the turn and let the PR event wake you.
- If CI fails, read only the failing job's log, reproduce locally, fix once.
- Don't read `LICENSE`, `build/` or STL/BREP output (blocked in `.claude/settings.json`).
- Don't use subagents or deep research unless asked.
