// P4 correctness tests. A local update is only useful if it gives exactly the
// result of rebuilding everything, and reports every byte it changed.
#include "cluster_mesh.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace meshify::p4;

namespace {

int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            ++failures;                                                                \
            std::cerr << "  CHECK failed at line " << __LINE__ << ": " #cond << "\n"; \
        }                                                                              \
    } while (0)

constexpr size_t kSize = 200000;

void meshIsClosedAndClustered() {
    const ClusterMesh m = ClusterMesh::makeSculpt(kSize, 3);
    const double v = static_cast<double>(m.vertexCount());
    CHECK(std::abs(v - kSize) < 0.02 * kSize);
    // A closed triangle mesh of genus 0 has 2 (V - 2) triangles.
    CHECK(m.triangleCount() == 2 * (m.vertexCount() - 2));

    size_t covered = 0;
    for (const Cluster& c : m.clusters()) {
        CHECK(c.vertexCount <= 64 && c.triangleCount <= 124);
        covered += c.triangleCount;
    }
    CHECK(covered == m.triangleCount());

    // Normals point outwards on a sphere-like surface.
    size_t outward = 0;
    for (size_t i = 0; i < m.vertexCount(); ++i) {
        const Vec3& p = m.positions()[i];
        const Vec3& n = m.normals()[i];
        if (p.x * n.x + p.y * n.y + p.z * n.z > 0) ++outward;
    }
    CHECK(outward > 0.99 * m.vertexCount());
}

bool sameRender(const std::vector<RenderVertex>& a, const std::vector<RenderVertex>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(RenderVertex)) == 0;
}

// After any run of edits, the locally updated render data and bounds must equal a
// full rebuild, bit for bit.
void localUpdateEqualsFullRebuild() {
    ClusterMesh m = ClusterMesh::makeSculpt(kSize, 5);
    std::mt19937 rng(11);
    std::uniform_int_distribution<size_t> pick(0, m.vertexCount() - 1);
    const float radii[] = {0.4f, 1.0f, 3.0f, 9.0f};
    for (int i = 0; i < 40; ++i) {
        const Vec3 c = m.positions()[pick(rng)];
        m.brush(c, radii[i % 4], (i % 2 ? 0.4f : -0.4f));
    }
    const std::vector<RenderVertex> local = m.render();
    const std::vector<ClusterBounds> localBounds = m.bounds();
    m.rebuildAll();
    CHECK(sameRender(local, m.render()));
    bool boundsEqual = localBounds.size() == m.bounds().size();
    for (size_t i = 0; boundsEqual && i < localBounds.size(); ++i) {
        boundsEqual = std::memcmp(&localBounds[i], &m.bounds()[i], sizeof(ClusterBounds)) == 0;
    }
    CHECK(boundsEqual);
}

// Every render vertex an edit changes must lie in a reported range, so uploading
// those ranges leaves the GPU copy identical to the CPU copy.
void reportedRangesCoverEveryChange() {
    ClusterMesh m = ClusterMesh::makeSculpt(kSize, 9);
    std::mt19937 rng(21);
    std::uniform_int_distribution<size_t> pick(0, m.vertexCount() - 1);
    for (int i = 0; i < 25; ++i) {
        const std::vector<RenderVertex> before = m.render();
        const EditResult r = m.brush(m.positions()[pick(rng)], i % 2 ? 2.0f : 0.6f, 0.3f);
        std::vector<char> covered(before.size(), 0);
        size_t bytes = 0;
        for (const auto& [first, count] : r.dirtyRanges) {
            for (size_t s = first; s < first + count; ++s) covered[s] = 1;
            bytes += count * sizeof(RenderVertex);
        }
        CHECK(bytes == r.dirtyBytes);
        size_t changed = 0, uncovered = 0;
        for (size_t s = 0; s < before.size(); ++s) {
            if (std::memcmp(&before[s], &m.render()[s], sizeof(RenderVertex)) != 0) {
                ++changed;
                if (!covered[s]) ++uncovered;
            }
        }
        CHECK(changed > 0);
        CHECK(uncovered == 0);
    }
}

void editsStayLocal() {
    ClusterMesh m = ClusterMesh::makeSculpt(kSize, 13);
    const EditResult r = m.brush(m.positions()[1234], 1.0f, 0.3f);
    CHECK(r.movedVertices > 0);
    CHECK(r.dirtyVertices > r.movedVertices);  // neighbours get new normals too
    CHECK(!r.dirtyClusters.empty());
    CHECK(r.dirtyClusters.size() < m.clusterCount() / 50);
    CHECK(r.dirtyBytes < m.render().size() * sizeof(RenderVertex) / 50);

    // A brush in empty space changes nothing.
    const EditResult none = m.brush({500, 500, 500}, 2.0f, 1.0f);
    CHECK(none.movedVertices == 0 && none.dirtyClusters.empty() && none.dirtyBytes == 0);
}

void editChangesTheSurface() {
    ClusterMesh m = ClusterMesh::makeSculpt(kSize, 17);
    const size_t v = 777;
    const Vec3 before = m.positions()[v];
    const Vec3 n = m.normals()[v];
    m.brush(before, 1.0f, 0.5f);
    const Vec3 after = m.positions()[v];
    // The vertex under the brush centre moves by the full depth along its normal.
    const float moved = (after.x - before.x) * n.x + (after.y - before.y) * n.y + (after.z - before.z) * n.z;
    CHECK(std::abs(moved - 0.5f) < 1e-3f);
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"mesh is closed and fully clustered", meshIsClosedAndClustered},
        {"local update equals full rebuild", localUpdateEqualsFullRebuild},
        {"reported ranges cover every change", reportedRangesCoverEveryChange},
        {"edits stay local", editsStayLocal},
        {"brush moves vertices by the requested depth", editChangesTheSurface},
    };
    for (const auto& [name, fn] : tests) {
        const int before = failures;
        std::cout << "[ RUN  ] " << name << std::endl;
        fn();
        std::cout << (failures == before ? "[  OK  ] " : "[ FAIL ] ") << name << std::endl;
    }
    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests failed\n");
    return failures == 0 ? 0 : 1;
}
