# P4 step 1: local brush edits on a clustered dense mesh

Question from ADR-0001: can a 5-million-vertex model take local edits and show them in under 50 ms? Most of that budget belongs to the GPU (upload and draw), so the CPU side must be small. This step measures the CPU side. The GPU side is step 2 and needs a machine with a graphics card.

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

- **No GPU numbers yet.** Nothing here measures upload, drawing, culling or frame time. Step 2 adds a Qt RHI viewer that does, to run on a real graphics card.
- **One machine.** The reference hardware (Windows, 32 GB) has not been measured.
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
