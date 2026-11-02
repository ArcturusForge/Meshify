// Measures brush-edit cost on a dense mesh. Run with --vertices 5000000 for the
// ADR-0001 benchmark size. Prints a markdown table.
//
// Pass condition: for every brush size, the 95th percentile of the CPU update
// stays within --budget-ms. This is the CPU share of the 50 ms edit-to-screen
// target; GPU upload and drawing are measured separately.
#include "cluster_mesh.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <random>
#include <string>

using namespace meshify::p4;
using Clock = std::chrono::steady_clock;

namespace {

double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5))];
}

}  // namespace

int main(int argc, char** argv) {
    size_t vertices = 5000000;
    int edits = 200;
    double budget = 15.0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--vertices")) vertices = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--edits")) edits = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--budget-ms")) budget = std::atof(argv[i + 1]);
    }

    const auto t0 = Clock::now();
    ClusterMesh mesh = ClusterMesh::makeSculpt(vertices, 1);
    const auto t1 = Clock::now();
    std::printf("Mesh: %zu vertices, %zu triangles, %zu clusters (about %.0f triangles each)\n", mesh.vertexCount(),
                mesh.triangleCount(), mesh.clusterCount(),
                static_cast<double>(mesh.triangleCount()) / static_cast<double>(mesh.clusterCount()));
    std::printf("Build: %.2f s total, of which clustering %.2f s. Memory: %.0f MB. Render buffer: %.0f MB.\n\n",
                ms(t0, t1) / 1000.0, mesh.clusterBuildSeconds(), static_cast<double>(mesh.memoryBytes()) / 1048576.0,
                static_cast<double>(mesh.render().size() * sizeof(RenderVertex)) / 1048576.0);

    // Baseline: what an edit costs without local updates.
    const auto b0 = Clock::now();
    mesh.rebuildAll();
    const double fullMs = ms(b0, Clock::now());

    std::mt19937 rng(7);
    // The grid is denser near the poles. Accepting a vertex with probability
    // sin(theta) makes the edit positions even over the surface, not over the vertices.
    std::uniform_int_distribution<size_t> pickVertex(0, mesh.vertexCount() - 1);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    auto pick = [&](std::mt19937&) {
        for (;;) {
            const size_t v = pickVertex(rng);
            const Vec3& p = mesh.positions()[v];
            const float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - (p.y / r) * (p.y / r)));
            if (unit(rng) < sinTheta) return v;
        }
    };
    const struct {
        const char* name;
        float radius;
    } brushes[] = {{"fine", 0.5f}, {"medium", 2.0f}, {"broad", 8.0f}};

    std::printf("| Brush | Radius (mm) | Moved vertices | Dirty clusters | Upload (KB) | p50 (ms) | p95 (ms) | max (ms) |\n");
    std::printf("| --- | --- | --- | --- | --- | --- | --- | --- |\n");
    bool pass = true;
    for (const auto& brush : brushes) {
        std::vector<double> times;
        double moved = 0, clusters = 0, kb = 0, find = 0, normals = 0, render = 0, bounds = 0;
        for (int i = 0; i < edits; ++i) {
            const size_t v = pick(rng);
            const Vec3 c = mesh.positions()[v];
            const float depth = (i % 2 ? 0.3f : -0.3f);
            const auto s = Clock::now();
            const EditResult r = mesh.brush(c, brush.radius, depth);
            const auto e = Clock::now();
            times.push_back(ms(s, e));
            moved += static_cast<double>(r.movedVertices);
            clusters += static_cast<double>(r.dirtyClusters.size());
            kb += static_cast<double>(r.dirtyBytes) / 1024.0;
            find += r.msFind;
            normals += r.msNormals;
            render += r.msRender;
            bounds += r.msBounds;
        }
        const double p95 = percentile(times, 0.95);
        if (p95 > budget) pass = false;
        std::printf("| %s | %.1f | %.0f | %.0f | %.0f | %.3f | %.3f | %.3f |\n", brush.name, brush.radius, moved / edits,
                    clusters / edits, kb / edits, percentile(times, 0.5), p95, *std::max_element(times.begin(), times.end()));
        std::printf("|  | average split (ms) | find %.2f | normals %.2f | render %.2f | bounds %.2f | | | |\n", find / edits, normals / edits, render / edits, bounds / edits);
    }
    std::printf("\nFull rebuild of every normal, vertex and bound (no local update): %.0f ms, %.0f MB to upload.\n", fullMs,
                static_cast<double>(mesh.render().size() * sizeof(RenderVertex)) / 1048576.0);
    std::printf("Budget for the CPU update: p95 <= %.1f ms. %s\n", budget, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
