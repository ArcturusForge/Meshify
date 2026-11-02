#include "cluster_mesh.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <type_traits>

namespace meshify::p4 {

namespace {

constexpr size_t kMaxClusterVertices = 64;
constexpr size_t kMaxClusterTriangles = 124;
constexpr float kPi = 3.14159265358979f;

float hash01(int x, int y, int z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u ^ static_cast<uint32_t>(y) * 0xd8163841u ^
                 static_cast<uint32_t>(z) * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return static_cast<float>(h & 0xffffffu) / 8388608.0f - 1.0f;  // [-1, 1)
}

float smooth(float t) { return t * t * (3 - 2 * t); }

float valueNoise(float x, float y, float z, uint32_t seed) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y)),
              zi = static_cast<int>(std::floor(z));
    const float fx = smooth(x - xi), fy = smooth(y - yi), fz = smooth(z - zi);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    float c[2][2][2];
    for (int dx = 0; dx < 2; ++dx)
        for (int dy = 0; dy < 2; ++dy)
            for (int dz = 0; dz < 2; ++dz) c[dx][dy][dz] = hash01(xi + dx, yi + dy, zi + dz, seed);
    return lerp(lerp(lerp(c[0][0][0], c[1][0][0], fx), lerp(c[0][1][0], c[1][1][0], fx), fy),
                lerp(lerp(c[0][0][1], c[1][0][1], fx), lerp(c[0][1][1], c[1][1][1], fx), fy), fz);
}

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Runs f(begin, end) over [0, n) on several threads when n is large enough to
// repay starting them; small edits stay on the calling thread.
template <class F>
void parallelFor(size_t n, size_t minPerThread, F&& f) {
    const size_t hw = std::max(1u, std::thread::hardware_concurrency());
    const size_t threads = std::min(hw, n / minPerThread);
    if (threads <= 1) {
        f(size_t{0}, n);
        return;
    }
    std::vector<std::thread> pool;
    const size_t chunk = (n + threads - 1) / threads;
    for (size_t t = 1; t < threads; ++t) {
        const size_t begin = t * chunk, end = std::min(n, begin + chunk);
        if (begin < end) pool.emplace_back([&f, begin, end] { f(begin, end); });
    }
    f(size_t{0}, std::min(n, chunk));
    for (std::thread& t : pool) t.join();
}

}  // namespace

ClusterMesh::ClusterMesh() = default;

ClusterMesh ClusterMesh::makeSculpt(size_t targetVertices, uint32_t seed) {
    ClusterMesh m;
    // R rings of 2R vertices plus two poles: about 2 R^2 vertices.
    const size_t R = std::max<size_t>(4, static_cast<size_t>(std::lround(std::sqrt(targetVertices / 2.0))));
    const size_t W = 2 * R;
    const size_t V = W * (R - 1) + 2;
    m.m_pos.resize(V);

    auto relief = [&](float dx, float dy, float dz) {
        // Three octaves, about 1 mm peak to peak at the coarsest.
        const float r0 = 40.0f;
        return r0 + 1.0f * valueNoise(dx * r0 * 0.15f, dy * r0 * 0.15f, dz * r0 * 0.15f, seed) +
               0.4f * valueNoise(dx * r0 * 0.6f, dy * r0 * 0.6f, dz * r0 * 0.6f, seed + 1) +
               0.15f * valueNoise(dx * r0 * 2.4f, dy * r0 * 2.4f, dz * r0 * 2.4f, seed + 2);
    };
    auto place = [&](size_t index, float dx, float dy, float dz) {
        const float r = relief(dx, dy, dz);
        m.m_pos[index] = {dx * r, dy * r, dz * r};
    };
    place(0, 0, 1, 0);
    place(1, 0, -1, 0);
    for (size_t r = 1; r < R; ++r) {
        const float theta = kPi * static_cast<float>(r) / static_cast<float>(R);
        const float st = std::sin(theta), ct = std::cos(theta);
        for (size_t c = 0; c < W; ++c) {
            const float phi = 2 * kPi * static_cast<float>(c) / static_cast<float>(W);
            place(2 + (r - 1) * W + c, st * std::cos(phi), ct, st * std::sin(phi));
        }
    }

    auto ring = [&](size_t r, size_t c) { return static_cast<uint32_t>(2 + (r - 1) * W + (c % W)); };
    m.m_tris.reserve(3 * 2 * (V - 2));
    auto tri = [&](uint32_t a, uint32_t b, uint32_t c) {
        m.m_tris.push_back(a);
        m.m_tris.push_back(b);
        m.m_tris.push_back(c);
    };
    for (size_t c = 0; c < W; ++c) {
        tri(0, ring(1, c + 1), ring(1, c));
        tri(1, ring(R - 1, c), ring(R - 1, c + 1));
    }
    for (size_t r = 1; r + 1 < R; ++r) {
        for (size_t c = 0; c < W; ++c) {
            const uint32_t a = ring(r, c), b = ring(r, c + 1), d = ring(r + 1, c), e = ring(r + 1, c + 1);
            tri(a, b, d);
            tri(b, e, d);
        }
    }
    // Make the triangles face outwards, whatever the grid handedness above.
    {
        const Vec3 &a = m.m_pos[m.m_tris[0]], &b = m.m_pos[m.m_tris[1]], &c = m.m_pos[m.m_tris[2]];
        const Vec3 n = cross(sub(b, a), sub(c, a));
        if (n.x * a.x + n.y * a.y + n.z * a.z < 0) {
            for (size_t i = 0; i < m.m_tris.size(); i += 3) std::swap(m.m_tris[i + 1], m.m_tris[i + 2]);
        }
    }

    m.m_nrm.resize(V);
    m.buildAdjacency();
    {
        std::vector<uint32_t> all(V);
        for (size_t i = 0; i < V; ++i) all[i] = static_cast<uint32_t>(i);
        m.buildNormals(all.data(), V);
    }
    const auto t0 = std::chrono::steady_clock::now();
    m.buildClusters();
    m.m_clusterSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m.m_vertexStamp.assign(V, 0);
    m.m_dirtyStamp.assign(V, 0);
    m.m_clusterStamp.assign(m.m_clusters.size(), 0);
    return m;
}

void ClusterMesh::buildAdjacency() {
    const size_t V = m_pos.size();
    m_vtxTriStart.assign(V + 1, 0);
    for (uint32_t v : m_tris) ++m_vtxTriStart[v + 1];
    for (size_t i = 0; i < V; ++i) m_vtxTriStart[i + 1] += m_vtxTriStart[i];
    m_vtxTri.resize(m_tris.size());
    std::vector<uint32_t> fill(m_vtxTriStart.begin(), m_vtxTriStart.end() - 1);
    for (size_t t = 0; t < m_tris.size(); ++t) m_vtxTri[fill[m_tris[t]]++] = static_cast<uint32_t>(t / 3);
}

// Area-weighted normal from the triangles around each listed vertex.
void ClusterMesh::buildNormals(const uint32_t* verts, size_t count) {
  parallelFor(count, 20000, [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
        const uint32_t v = verts[i];
        Vec3 sum;
        for (uint32_t k = m_vtxTriStart[v]; k < m_vtxTriStart[v + 1]; ++k) {
            const uint32_t* t = &m_tris[3 * static_cast<size_t>(m_vtxTri[k])];
            const Vec3 n = cross(sub(m_pos[t[1]], m_pos[t[0]]), sub(m_pos[t[2]], m_pos[t[0]]));
            sum.x += n.x;
            sum.y += n.y;
            sum.z += n.z;
        }
        const float len = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
        m_nrm[v] = len > 0 ? Vec3{sum.x / len, sum.y / len, sum.z / len} : Vec3{0, 1, 0};
    }
  });
}

void ClusterMesh::buildClusters() {
    const size_t V = m_pos.size();
    const size_t bound = meshopt_buildMeshletsBound(m_tris.size(), kMaxClusterVertices, kMaxClusterTriangles);
    std::vector<meshopt_Meshlet> meshlets(bound);
    m_clusterVerts.resize(bound * kMaxClusterVertices);
    m_clusterTris.resize(bound * kMaxClusterTriangles * 3);
    const size_t n = meshopt_buildMeshlets(meshlets.data(), m_clusterVerts.data(), m_clusterTris.data(), m_tris.data(),
                                           m_tris.size(), &m_pos[0].x, V, sizeof(Vec3), kMaxClusterVertices,
                                           kMaxClusterTriangles, 0.0f);
    meshlets.resize(n);
    const meshopt_Meshlet& last = meshlets.back();
    m_clusterVerts.resize(last.vertex_offset + last.vertex_count);
    m_clusterTris.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3u));
    m_clusterVerts.shrink_to_fit();
    m_clusterTris.shrink_to_fit();

    m_clusters.resize(n);
    m_slotCluster.resize(m_clusterVerts.size());
    for (size_t i = 0; i < n; ++i) {
        m_clusters[i] = {meshlets[i].vertex_offset, meshlets[i].vertex_count, meshlets[i].triangle_offset,
                         meshlets[i].triangle_count};
        std::fill_n(m_slotCluster.begin() + meshlets[i].vertex_offset, meshlets[i].vertex_count, static_cast<uint32_t>(i));
    }

    m_vtxSlotStart.assign(V + 1, 0);
    for (uint32_t v : m_clusterVerts) ++m_vtxSlotStart[v + 1];
    for (size_t i = 0; i < V; ++i) m_vtxSlotStart[i + 1] += m_vtxSlotStart[i];
    m_vtxSlot.resize(m_clusterVerts.size());
    std::vector<uint32_t> fill(m_vtxSlotStart.begin(), m_vtxSlotStart.end() - 1);
    for (size_t s = 0; s < m_clusterVerts.size(); ++s) m_vtxSlot[fill[m_clusterVerts[s]]++] = static_cast<uint32_t>(s);

    m_render.resize(m_clusterVerts.size());
    for (size_t s = 0; s < m_render.size(); ++s) {
        const uint32_t v = m_clusterVerts[s];
        m_render[s] = {{m_pos[v].x, m_pos[v].y, m_pos[v].z}, {m_nrm[v].x, m_nrm[v].y, m_nrm[v].z}};
    }
    m_bounds.resize(n);
    parallelFor(n, 256, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) refreshBounds(static_cast<uint32_t>(i));
    });
}

void ClusterMesh::refreshBounds(uint32_t cluster) {
    const Cluster& c = m_clusters[cluster];
    const meshopt_Bounds b = meshopt_computeMeshletBounds(&m_clusterVerts[c.vertexOffset], &m_clusterTris[c.triangleOffset],
                                                          c.triangleCount, &m_pos[0].x, m_pos.size(), sizeof(Vec3));
    m_bounds[cluster] = {{b.center[0], b.center[1], b.center[2]}, b.radius};
}

void ClusterMesh::rebuildAll() {
    std::vector<uint32_t> all(m_pos.size());
    for (size_t i = 0; i < all.size(); ++i) all[i] = static_cast<uint32_t>(i);
    buildNormals(all.data(), all.size());
    for (size_t s = 0; s < m_render.size(); ++s) {
        const uint32_t v = m_clusterVerts[s];
        m_render[s] = {{m_pos[v].x, m_pos[v].y, m_pos[v].z}, {m_nrm[v].x, m_nrm[v].y, m_nrm[v].z}};
    }
    for (size_t i = 0; i < m_clusters.size(); ++i) refreshBounds(static_cast<uint32_t>(i));
}

namespace {
using Clock = std::chrono::steady_clock;
double since(Clock::time_point& t) {
    const auto now = Clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - t).count();
    t = now;
    return ms;
}
}  // namespace

EditResult ClusterMesh::brush(const Vec3& center, float radius, float depth) {
    EditResult out;
    auto clock = Clock::now();
    ++m_editId;
    const float r2 = radius * radius;

    // 1. Vertices inside the brush. A cluster whose bounding sphere misses the
    //    brush cannot hold one, so most of the mesh is skipped without a look.
    std::vector<Move>& moves = m_moves;
    moves.clear();
    for (size_t ci = 0; ci < m_clusters.size(); ++ci) {
        const ClusterBounds& b = m_bounds[ci];
        const float dx = b.center[0] - center.x, dy = b.center[1] - center.y, dz = b.center[2] - center.z;
        const float reach = radius + b.radius;
        if (dx * dx + dy * dy + dz * dz > reach * reach) continue;
        const Cluster& c = m_clusters[ci];
        for (uint32_t s = c.vertexOffset; s < c.vertexOffset + c.vertexCount; ++s) {
            const uint32_t v = m_clusterVerts[s];
            if (m_vertexStamp[v] == m_editId) continue;
            const Vec3& p = m_pos[v];
            const float ex = p.x - center.x, ey = p.y - center.y, ez = p.z - center.z;
            const float d2 = ex * ex + ey * ey + ez * ez;
            if (d2 >= r2) continue;
            m_vertexStamp[v] = m_editId;
            const float t = 1.0f - std::sqrt(d2) / radius;
            const float k = depth * smooth(t);
            moves.push_back({v, {p.x + m_nrm[v].x * k, p.y + m_nrm[v].y * k, p.z + m_nrm[v].z * k}});
        }
    }
    out.movedVertices = moves.size();
    if (moves.empty()) return out;
    out.msFind = since(clock);

    // 2. Apply the moves. Normals change for the moved vertices and their neighbours.
    for (const Move& m : moves) m_pos[m.vertex] = m.position;
    std::vector<uint32_t>& dirty = m_dirtyList;
    dirty.clear();
    auto mark = [&](uint32_t v) {
        if (m_dirtyStamp[v] != m_editId) {
            m_dirtyStamp[v] = m_editId;
            dirty.push_back(v);
        }
    };
    for (const Move& m : moves) {
        for (uint32_t k = m_vtxTriStart[m.vertex]; k < m_vtxTriStart[m.vertex + 1]; ++k) {
            const uint32_t* t = &m_tris[3 * static_cast<size_t>(m_vtxTri[k])];
            mark(t[0]);
            mark(t[1]);
            mark(t[2]);
        }
    }
    buildNormals(dirty.data(), dirty.size());
    out.dirtyVertices = dirty.size();
    out.msNormals = since(clock);

    // 3. Refresh the render copy of every dirty vertex and note which clusters hold them.
    std::vector<uint32_t>& clusters = m_clusterList;
    clusters.clear();
    for (uint32_t v : dirty) {
        const RenderVertex rv{{m_pos[v].x, m_pos[v].y, m_pos[v].z}, {m_nrm[v].x, m_nrm[v].y, m_nrm[v].z}};
        for (uint32_t k = m_vtxSlotStart[v]; k < m_vtxSlotStart[v + 1]; ++k) {
            const uint32_t slot = m_vtxSlot[k];
            m_render[slot] = rv;
            const uint32_t cl = m_slotCluster[slot];
            if (m_clusterStamp[cl] != m_editId) {
                m_clusterStamp[cl] = m_editId;
                clusters.push_back(cl);
            }
        }
    }
    std::sort(clusters.begin(), clusters.end());
    out.msRender = since(clock);
    parallelFor(clusters.size(), 256, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) refreshBounds(clusters[i]);
    });
    out.msBounds = since(clock);

    // 4. The byte ranges a GPU upload must cover: whole clusters, merged when adjacent.
    for (uint32_t cl : clusters) {
        const Cluster& c = m_clusters[cl];
        if (!out.dirtyRanges.empty() && out.dirtyRanges.back().first + out.dirtyRanges.back().second == c.vertexOffset) {
            out.dirtyRanges.back().second += c.vertexCount;
        } else {
            out.dirtyRanges.emplace_back(c.vertexOffset, c.vertexCount);
        }
        out.dirtyBytes += static_cast<size_t>(c.vertexCount) * sizeof(RenderVertex);
    }
    out.dirtyClusters = clusters;
    return out;
}

size_t ClusterMesh::memoryBytes() const {
    auto bytes = [](const auto& v) { return v.capacity() * sizeof(typename std::decay_t<decltype(v)>::value_type); };
    return bytes(m_pos) + bytes(m_nrm) + bytes(m_tris) + bytes(m_vtxTriStart) + bytes(m_vtxTri) + bytes(m_vtxSlotStart) +
           bytes(m_vtxSlot) + bytes(m_slotCluster) + bytes(m_clusters) + bytes(m_clusterVerts) + bytes(m_clusterTris) +
           bytes(m_render) + bytes(m_bounds) + bytes(m_vertexStamp) + bytes(m_dirtyStamp) + bytes(m_clusterStamp);
}

}  // namespace meshify::p4
