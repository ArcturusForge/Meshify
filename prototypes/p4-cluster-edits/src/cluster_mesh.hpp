// A dense mesh split into small render clusters, with local brush edits.
//
// The question behind P4 is whether an edit to a multi-million-vertex model can
// reach the screen in under 50 ms. Most of that budget belongs to the GPU
// upload and draw, so the CPU side has to be small: a brush edit may only touch
// the clusters it affects, and must report exactly which bytes of the render
// buffer changed so that only those are uploaded.
//
// Authoritative geometry (`positions`, `normals`, triangles) is kept apart from
// the render copy, which is laid out per cluster the way a GPU buffer would be:
// each cluster owns a contiguous block of vertices. A vertex on a cluster border
// appears in each cluster that uses it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace meshify::p4 {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct RenderVertex {
    float p[3];
    float n[3];
};

struct ClusterBounds {
    float center[3];
    float radius;
};

struct Cluster {
    uint32_t vertexOffset;  // first slot in the render buffer
    uint32_t vertexCount;
    uint32_t triangleOffset;  // into the cluster triangle list
    uint32_t triangleCount;
};

struct EditResult {
    size_t movedVertices = 0;   // vertices displaced by the brush
    size_t dirtyVertices = 0;   // vertices whose position or normal changed
    std::vector<uint32_t> dirtyClusters;                 // sorted
    std::vector<std::pair<size_t, size_t>> dirtyRanges;  // [first slot, slot count), merged
    size_t dirtyBytes = 0;                                // render bytes to upload
    // Where the time went, in milliseconds.
    double msFind = 0, msNormals = 0, msRender = 0, msBounds = 0;
};

class ClusterMesh {
public:
    ClusterMesh();

    // A closed sphere-like surface of roughly `targetVertices` vertices with
    // fractal relief, like a dense sculpt. Radius about 40 mm. Units are mm.
    static ClusterMesh makeSculpt(size_t targetVertices, uint32_t seed);

    // Displaces vertices within `radius` of `center` along their normals by up to
    // `depth` (smooth falloff; negative digs in) and updates only what that changes.
    EditResult brush(const Vec3& center, float radius, float depth);

    // Recomputes every normal, render vertex and bound from the authoritative
    // positions. The slow reference path used to check `brush`.
    void rebuildAll();

    size_t vertexCount() const { return m_pos.size(); }
    size_t triangleCount() const { return m_tris.size() / 3; }
    size_t clusterCount() const { return m_clusters.size(); }
    size_t memoryBytes() const;

    const std::vector<Vec3>& positions() const { return m_pos; }
    const std::vector<Vec3>& normals() const { return m_nrm; }
    const std::vector<uint32_t>& triangles() const { return m_tris; }
    const std::vector<RenderVertex>& render() const { return m_render; }
    const std::vector<Cluster>& clusters() const { return m_clusters; }
    const std::vector<ClusterBounds>& bounds() const { return m_bounds; }
    const std::vector<uint32_t>& clusterVertices() const { return m_clusterVerts; }
    const std::vector<uint8_t>& clusterTriangles() const { return m_clusterTris; }

    double clusterBuildSeconds() const { return m_clusterSeconds; }

private:
    void buildNormals(const uint32_t* verts, size_t count);
    void buildAdjacency();
    void buildClusters();
    void refreshBounds(uint32_t cluster);

    std::vector<Vec3> m_pos, m_nrm;
    std::vector<uint32_t> m_tris;

    // Triangles around each vertex (CSR).
    std::vector<uint32_t> m_vtxTriStart, m_vtxTri;
    // Render slots holding each vertex (CSR), and the cluster owning each slot.
    std::vector<uint32_t> m_vtxSlotStart, m_vtxSlot, m_slotCluster;

    std::vector<Cluster> m_clusters;
    std::vector<uint32_t> m_clusterVerts;  // slot -> vertex
    std::vector<uint8_t> m_clusterTris;    // local indices, 3 per triangle
    std::vector<RenderVertex> m_render;
    std::vector<ClusterBounds> m_bounds;

    // Scratch stamps, so each edit can mark vertices and clusters without clearing arrays.
    std::vector<uint32_t> m_vertexStamp, m_dirtyStamp, m_clusterStamp;
    uint32_t m_editId = 0;
    // Reused between edits so a large brush does not pay for allocating and faulting in memory.
    struct Move {
        uint32_t vertex;
        Vec3 position;
    };
    std::vector<Move> m_moves;
    std::vector<uint32_t> m_dirtyList, m_clusterList;
    double m_clusterSeconds = 0;
};

}  // namespace meshify::p4
