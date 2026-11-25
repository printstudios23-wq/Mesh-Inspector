// SPDX-License-Identifier: Apache-2.0
// Mesh quality analysis: welding, topology checks and triangle quality checks.
#pragma once

#include <cstdint>
#include <vector>

#include "mesh.h"

namespace mi {

struct AnalysisSettings {
    // Vertices closer than (weldTolerance * bounding-box diagonal) are treated as the same
    // point. This is what lets STL "triangle soup" and OBJ files with split UV seams be
    // checked topologically. It is also the thickness below which a triangle counts as degenerate.
    double weldTolerance = 1e-6;
    // Triangles whose smallest angle is below this are reported as slivers (informational).
    double sliverAngleDeg = 1.0;
};

struct HoleLoop {
    uint32_t edgeCount = 0;
    bool closed = true;  // false if the chain of open edges does not close up
    double perimeter = 0;
    Vec3 center;
    float radius = 0;
};

struct MeshReport {
    // --- file level
    size_t fileVertices = 0;
    size_t fileTriangles = 0;
    size_t nonFiniteVertices = 0;

    // --- after welding
    size_t weldedVertices = 0;
    size_t triangles = 0;  // triangles that take part in the topology (non-collapsed)
    size_t looseVertices = 0;  // unique points not used by any triangle
    double weldDistance = 0;   // absolute welding distance that was used

    // --- triangle quality
    size_t degenerateTriangles = 0;  // = collapsed + zeroArea + invalid
    size_t collapsedTriangles = 0;   // two or more corners weld to the same point
    size_t zeroAreaTriangles = 0;    // three distinct corners but (nearly) collinear
    size_t invalidTriangles = 0;     // NaN / infinite coordinates
    size_t sliverTriangles = 0;
    size_t duplicateTriangles = 0;

    // --- topology
    size_t edges = 0;
    size_t boundaryEdges = 0;      // used by exactly one triangle  -> open gaps
    size_t nonManifoldEdges = 0;   // used by three or more triangles
    size_t inconsistentEdges = 0;  // two triangles traverse the edge in the same direction (flipped neighbour)
    size_t holeLoops = 0;          // number of boundary loops / chains
    size_t openChains = 0;         // chains that do not close (subset of holeLoops)
    size_t shells = 0;             // connected components
    long long eulerCharacteristic = 0;
    long long genus = 0;           // only meaningful when closedManifold && consistent

    bool watertight = false;        // no boundary edges and no non-manifold edges
    bool consistentWinding = false;
    bool closedManifold = false;    // watertight && consistent winding
    bool normalsOutward = false;    // volume > 0 (only meaningful when closedManifold)

    // --- geometry
    AABB bounds;
    double surfaceArea = 0;
    double volume = 0;  // signed; negative means the normals point inward

    std::vector<HoleLoop> holes;  // largest perimeter first, capped (see holeLoops for the real count)
};

// Welded geometry that is handed to the renderer.
struct RenderMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;  // smooth, angle-weighted
    std::vector<Vec3> colors;   // empty when the file has no colours
    std::vector<uint32_t> indices;
};

// Index lists into RenderMesh::positions that are drawn as problem highlights.
struct Overlays {
    std::vector<uint32_t> boundaryEdges;      // pairs
    std::vector<uint32_t> nonManifoldEdges;   // pairs
    std::vector<uint32_t> inconsistentEdges;  // pairs
    std::vector<uint32_t> degenerateEdges;    // pairs (zero-area triangles with distinct corners)
    std::vector<uint32_t> sliverTriangles;    // triples
    std::vector<Vec3> degenerateMarkers;      // one point per degenerate triangle
};

struct AnalyzedMesh {
    RenderMesh render;
    Overlays overlays;
    MeshReport report;
};

AnalyzedMesh analyzeMesh(const Mesh& mesh, const AnalysisSettings& settings);

}  // namespace mi
