# P5: a Python plugin operation with undo, save and reopen

Question from ADR-0001: can an operation written in Python take part in undo, save and reopen, and fail safely? The pass condition is two acceptance scenarios from the design brief:

- **HIS-01:** a heavy operation fails or crashes, and the last committed model is still there with nothing half-committed.
- **EXT-01:** a file is reopened with a required plugin absent; the saved geometry opens and the dependent operations are identified.

## Approach

- **Plugins run in a separate Python process.** The app sends one JSON request on stdin (plugin entry file, operation, parameters in mm, current geometry) and reads one JSON reply. `plugins/meshify_host.py` is the host; a plugin is a folder with `plugin.json` (name, version, API version, operations) and `plugin.py` with an `OPERATIONS` table of plain functions.
- **The app only accepts a validated result.** A reply must be valid JSON, finite numbers within 1,000 m, triangle indices in range. Anything else (an exception, a crash, a hang past the timeout, bad output) is an error message and nothing is committed.
- **Each committed step stores its result.** A step records plugin name and version, operation, parameters and the geometry after it. Undo and redo move a cursor and never need the plugin. A new step after an undo drops the redo branch.
- **Save is atomic** (`QSaveFile`: write a temporary file, rename on success). **Open is all or nothing**: a file that is cut off, not JSON, or has bad geometry is refused whole, and the caller's document is left alone.
- **Reopen compares the file with the installed plugins.** Each step is reported as fine, plugin not installed, plugin version differs, or operation missing. `replay` re-runs every step from its recorded parameters and compares with the saved result, and stops at the first step that cannot run.
- The document is a toy: a box, a translate, and the example plugin's linear array. The point is the history and plugin contract, not geometry.

## Tests

`tests/plugin_history_tests.cpp` (needs Python 3; set `MESHIFY_PYTHON` to choose one):

| Test | What it proves |
| --- | --- |
| Undo and redo | Native and plugin steps undo and redo to bit-identical geometry; a new step drops the redo branch. |
| HIS-01 failures | Nine failure cases (raises, missing parameter, crash with exit code 3, hang stopped by a 1.5 s timeout, triangle index out of range, NaN output, unknown operation, unknown plugin): each gives a clear message, and geometry, step count and cursor are unchanged. A plugin that prints to stdout still works. |
| Save and reopen | Geometry matches bit for bit (including 1/3 mm), the cursor survives so redo still works, no temporary file is left, replay from parameters reproduces the saved results exactly. |
| EXT-01 plugin absent | The saved geometry opens; the step is flagged "plugin 'array_tools' 1.0.0 is not installed; this step keeps its saved result but cannot be recomputed or edited"; undo and redo cross it; native work continues; replay stops at the step and names it; saving again keeps the plugin records. |
| EXT-01 version differs | The step is flagged with both versions; replay still succeeds. |
| Damaged files | Truncated, empty, bad triangle index and missing files are refused with a reason, and the target document is untouched. |

## Cost of running an operation in Python

`p5_plugin_bench` (4-core Xeon VM, Python 3.13, 15 calls per row). The operation doubles the mesh. The save and open columns cover the whole history of that run (every step stores its full result), so the 100k row is a 5-step history of about 311,000 vertices in total.

| Input vertices | Result vertices | Call p50 (ms) | p95 (ms) | Save (ms) | File (MB) | Open (ms) |
| --- | --- | --- | --- | --- | --- | --- |
| 8 | 16 | 28 | 45 | 1 | 0.0 | 0 |
| 1,000 | 2,000 | 35 | 48 | 3 | 0.1 | 1 |
| 10,000 | 20,000 | 87 | 116 | 42 | 1.0 | 21 |
| 100,000 | 200,000 | 602 | 842 | 412 | 11.1 | 179 |

## Findings

1. **The contract works.** An operation written in Python takes part in undo, save and reopen, and none of nine ways of failing can change the committed model. A separate process makes this cheap to guarantee: the plugin has no way to reach the document. It is not a security boundary (the plugin can still read and write files), and the brief says so too.
2. **Starting Python costs about 30 ms per call.** That is fine for an operation the user applies. It is too slow to call per brush stroke.
3. **JSON over a pipe does not scale to dense meshes.** Time grows roughly linearly: about 0.6 s for a 100,000-vertex input, so a 5-million-vertex mesh would take around 30 s each way. Large geometry needs a binary hand-off (shared memory or a memory-mapped file) with the plugin reading arrays in place, for example as NumPy views. This is the main open question the next step would answer.
4. **Storing every full result is wasteful.** The 100k history is 11 MB for five steps. The brief's plan (shared unchanged geometry, local differences, checkpoints) is needed, and the P4 cluster layout is a natural unit for sharing.
5. **A missing plugin is a normal state, not an error.** Every history step carries the plugin name and version it needs, so the open dialog can list them before anything runs.

## Not covered, and limits

- **Heavy operations run on the calling thread** here. The real app must run them in the background and keep them cancellable (the P2 lesson); the timeout kill is the only cancellation tested.
- **A crash between two saves loses uncommitted steps.** Atomic save protects the file on disk, but there is no autosave or journal yet.
- **A file with one bad step is refused whole.** Recovering the good steps around it is not done.
- **No permissions or isolation beyond the process boundary.**
- **Replay equality is exact.** A plugin that gives slightly different floating-point results on another machine would be reported as "different"; a tolerance policy is not designed.
- **No preview, parameter editing, suppression or reordering** of recorded steps.
- **No embedded Python.** Embedding (pybind11) would be faster per call but ties the app to one Python build on three platforms; the subprocess design avoids that. Not compared here.

## Build and run

```sh
cmake --build build --target p5_history_tests p5_plugin_bench
ctest --test-dir build -R p5 --output-on-failure
./build/prototypes/p5-plugin-history/p5_plugin_bench --calls 15
```
