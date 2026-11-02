# P4: local brush edits on a clustered dense mesh

Question from ADR-0001: can a 5-million-vertex model take local edits and show them in under 50 ms? Most of that budget belongs to the GPU (upload and draw), so the CPU side must be small. Step 1 measures the CPU side. Step 2, the viewer below, measures the whole path through a GPU. It needs a machine with a graphics card, so the final numbers come from Pixel's machine.

## What it does

`ClusterMesh` holds a dense closed surface with fractal relief, like a sculpt. The authoritative data (positions, normals, triangles) is kept apart from a **render copy** laid out the way a GPU buffer would be: the mesh is split into clusters of up to 64 vertices and 124 triangles (built with [meshoptimizer](https://github.com/zeux/meshoptimizer), MIT), and each cluster owns a contiguous block of the render buffer.

`brush(center, radius, depth)` displaces the vertices inside the brush along their normals. It then updates only what changed:

1. Cluster bounding spheres rule out almost every cluster without looking at its vertices.
2. Normals are recomputed for the moved vertices and their neighbours only.
3. The render copy and bounds of the affected clusters are refreshed.
4. The call returns the exact byte ranges of the render buffer that changed, so a GPU upload can send only those.

Normal and bound recomputation runs on several threads when an edit is large, and scratch buffers are reused between edits.

## Tests

`p4_cluster_tests` (5 tests, 200k-vertex meshes):

| Test | Checks |
| --- | --- |
| mesh is closed and fully clustered | Triangle count is 2 (V - 2); every triangle is in exactly one cluster; normals point outwards |
| local update equals full rebuild | After 40 mixed edits, the render data and bounds are identical, bit for bit, to rebuilding everything |
| reported ranges cover every change | Over 25 edits, every render vertex that changed lies inside a reported range |
| edits stay local | A 1 mm brush dirties under 2% of the clusters; a brush in empty space changes nothing |
| brush moves vertices by the requested depth | The vertex under the brush centre moves by exactly the depth |

The second and third tests are the important ones: a fast update is worthless if it can leave stale geometry on screen.

## Results

Measured on a 4-core 2.1 GHz Xeon cloud VM with no graphics card (Release build, 150 edits per row, edit positions even over the surface). Brush radii are fixed in millimetres on an 80 mm object, so the same brush covers more vertices as the mesh gets denser.

p50 / p95 of the CPU update, in milliseconds:

| Vertices | Clusters | Build (s) | Fine, 0.5 mm | Medium, 2 mm | Broad, 8 mm | Full rebuild (ms) |
| --- | --- | --- | --- | --- | --- | --- |
| 1M | 21,532 | 1.4 | 0.09 / 0.12 | 0.24 / 0.55 | 2.4 / 5.3 | 112 |
| **5M** | 108,211 | 7.9 | 0.30 / 0.40 | 0.93 / 2.3 | 7.7 / 17.7 | 654 |
| 10M | 216,624 | 21.4 | 0.57 / 0.84 | 1.8 / 4.4 | 17.3 / 39.5 | 1,440 |
| 20M | 433,850 | 46.0 | 1.1 / 1.3 | 3.3 / 6.1 | 28.1 / 96.7 | 2,868 |

At 5M vertices, what an edit has to upload to the GPU:

| Brush | Vertices moved | Clusters changed | Upload |
| --- | --- | --- | --- |
| Fine | 177 | 11 | 17 KB |
| Medium | 3,261 | 96 | 144 KB |
| Broad | 48,523 | 1,141 | 1.7 MB |
| (no local update) | 4,995,962 | 108,211 | 159 MB |

Memory at 5M: 689 MB (159 MB of it the render buffer); 2.8 GB at 20M.

## Findings

1. **Local updates work and are exact.** A fine or medium brush costs 0.3 to 2.3 ms at 5M vertices, about 300 to 2,000 times less than rebuilding, and uploads kilobytes instead of 159 MB. Fine and medium brushes stay under 7 ms (p95) even at 20M vertices.
2. **A broad brush is the problem.** At 5M vertices the 8 mm brush moves about 48,000 vertices and takes 7.7 ms typically but 17.7 ms at p95 and up to 52 ms at worst, which is over the 15 ms CPU share I set aside from the 50 ms target. At 10M and 20M it is clearly over. Time goes mostly to normals (5 ms) and cluster bounds (3 ms) for that many vertices.
3. **So broad strokes need a reduced-detail preview.** The brief already allows "a labelled approximate preview, followed by an accurate computation". The likely design is to apply broad strokes to a coarser level of detail first and refine in the background. That needs a level-of-detail hierarchy, which this step does not have.
4. **Clustering is an offline step.** Building clusters takes 7 s at 5M and 43 s at 20M. Brush edits never change the clusters, but an edit that changes the mesh's topology (cutting, remeshing, a boolean) would need local re-clustering. That is not tested here.

## Not measured, and limits

- **One GPU, and a weak one.** The only real GPU run is on an Intel integrated GPU under Linux (below). The reference hardware (Windows, 32 GB) has not been measured.
- **Shape and edit type.** The surface is a sphere-like grid and edits are displacement only. Normals use incident-face weighting. A real sculpt is less regular.
- **Single level of detail.** No hierarchy, streaming or occlusion culling.

## Build and run

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target p4_cluster_tests p4_edit_bench
./build/prototypes/p4-cluster-edits/p4_cluster_tests
./build/prototypes/p4-cluster-edits/p4_edit_bench --vertices 5000000 --edits 200 --budget-ms 15
```

`p4_edit_bench` exits 1 if any brush's p95 exceeds the budget. At 5M vertices it reports FAIL because of the broad brush, which is the finding above. `ctest` runs a 500k-vertex version that must pass, so the check runs on every system in CI. meshoptimizer is fetched by CMake.

## Step 2: the viewer (`meshify_p4_viewer`)

A Qt RHI viewer (Qt 6.7 or newer, `QRhiWidget`) that draws the mesh and times edits end to end. The vertex buffer is the cluster-ordered render copy; the index buffer is built once. An edit uploads only the byte ranges `brush()` reports, then draws.

```sh
meshify_p4_viewer --vertices 5000000          # interactive
meshify_p4_viewer --vertices 5000000 --bench  # scripted edits, prints a table
```

- **Interactive:** left mouse sculpts (hold Shift to dig in), right mouse orbits, wheel zooms. The window title shows the cost of the last edit and the frame time.
- **`--bench`:** applies scripted edits with three brush sizes. For each one it measures the time from the start of the edit until the GPU has finished drawing the result and the image is read back (`grabFramebuffer`). It also reports one frame with no edit, for comparison, and the first frame with the full upload. It exits with code 1 if the 95th percentile of that time exceeds 50 ms for any brush.
- **`--shot out.png`:** saves a picture after some large edits, and checks that the picture drawn from partial uploads is identical to one drawn after uploading everything. That is the check that the GPU copy stays in step with the CPU copy.
- **`--api opengl`** forces OpenGL. The default is Direct3D 11 on Windows, Metal on macOS and OpenGL on Linux. The conda-forge Qt used for releases has no Vulkan support.

The release archive includes it as `bin/meshify_p4_viewer`.

### What was checked here

The sandbox has no graphics card, so the viewer ran on Mesa's software renderer (llvmpipe) under a virtual display. That shows the viewer works: the mesh draws correctly, edits appear, and partial uploads match a full upload. It says nothing about speed, and the frame times from it should be ignored.

### Results on a real GPU

Intel HD Graphics 630 (integrated), Mesa 26.2.4, OpenGL 4.6, Linux (Bazzite), 5M vertices (4,995,962 vertices, 9,991,920 triangles), 1280x720. First frame with the full 159 MB upload: 38 ms. One frame drawn and read back with no edit: p50 18.2 ms, p95 18.9 ms.

| Brush | Radius (mm) | Upload (KB) | CPU update p50 (ms) | Edit to GPU done p50 (ms) | p95 (ms) | max (ms) |
| --- | --- | --- | --- | --- | --- | --- |
| fine | 0.5 | 16 | 0.42 | 18.8 | 19.4 | 20.3 |
| medium | 2.0 | 153 | 1.04 | 19.8 | 22.5 | 25.6 |
| broad | 8.0 | 1856 | 7.73 | 29.6 | 42.1 | 71.6 |

The 50 ms target on p95 passes for all three brushes, on an integrated GPU. Most of the time for fine and medium brushes is the frame itself (drawing 10 million triangles in one call plus readback, 18 ms); the edit adds 0.6 and 1.6 ms. The broad brush adds about 11 ms at p50, but its worst case (71.6 ms) is over 50 ms, so broad strokes still want the reduced-detail preview from finding 3.

### Limits of the viewer

- It draws every triangle in one call. There is no cluster culling, level of detail or occlusion culling, so drawing time is the plain cost of the whole mesh. A 5M-vertex mesh is about 10 million triangles.
- The readback in `--bench` adds a little (a few ms at 1280x720) to every figure, including the no-edit frame; the difference between the two is the cost of the edit itself.
- It measures until the GPU finishes, not until the pixels reach the display, so display refresh and compositor delay are not counted.
