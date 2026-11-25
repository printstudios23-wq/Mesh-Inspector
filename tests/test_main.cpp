// SPDX-License-Identifier: Apache-2.0
// Self-contained tests (no framework): mesh analysis results and the file loaders.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "analysis.h"
#include "mesh_io.h"
#include "objects.h"
#include "settings.h"

using namespace mi;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                              \
    do {                                                                                                   \
        ++g_checks;                                                                                        \
        const double va = (a), vb = (b);                                                                   \
        if (!(std::fabs(va - vb) <= (eps))) {                                                              \
            ++g_failures;                                                                                  \
            std::printf("    FAIL %s:%d: %s (=%g) != %s (=%g)\n", __FILE__, __LINE__, #a, va, #b, vb);       \
        }                                                                                                  \
    } while (0)

// ---------------------------------------------------------------- test meshes

// Unit cube, counter-clockwise (outward) winding, 8 shared vertices, 12 triangles.
static Mesh makeCube() {
    Mesh m;
    m.positions = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    m.indices = {0, 3, 2, 0, 2, 1,   // bottom
                 4, 5, 6, 4, 6, 7,   // top
                 0, 1, 5, 0, 5, 4,   // front
                 2, 3, 7, 2, 7, 6,   // back
                 0, 4, 7, 0, 7, 3,   // left
                 1, 2, 6, 1, 6, 5};  // right
    return m;
}

// Same cube as an STL-style triangle soup (every triangle owns its 3 vertices).
static Mesh makeCubeSoup() {
    const Mesh c = makeCube();
    Mesh m;
    for (uint32_t i : c.indices) {
        m.positions.push_back(c.positions[i]);
        m.indices.push_back(static_cast<uint32_t>(m.positions.size() - 1));
    }
    return m;
}

static AnalyzedMesh analyze(const Mesh& m) { return analyzeMesh(m, AnalysisSettings{}); }

// ---------------------------------------------------------------- analysis tests

static void testCleanCube() {
    const AnalyzedMesh a = analyze(makeCube());
    const MeshReport& r = a.report;
    CHECK(r.weldedVertices == 8);
    CHECK(r.triangles == 12);
    CHECK(r.edges == 18);
    CHECK(r.boundaryEdges == 0);
    CHECK(r.nonManifoldEdges == 0);
    CHECK(r.inconsistentEdges == 0);
    CHECK(r.degenerateTriangles == 0);
    CHECK(r.duplicateTriangles == 0);
    CHECK(r.holeLoops == 0);
    CHECK(r.shells == 1);
    CHECK(r.eulerCharacteristic == 2);
    CHECK(r.genus == 0);
    CHECK(r.watertight && r.consistentWinding && r.closedManifold && r.normalsOutward);
    CHECK_NEAR(r.volume, 1.0, 1e-9);
    CHECK_NEAR(r.surfaceArea, 6.0, 1e-9);
    CHECK_NEAR(r.bounds.size().x, 1.0, 1e-6);
    CHECK(a.render.positions.size() == 8 && a.render.normals.size() == 8 && a.render.indices.size() == 36);
}

static void testCubeSoupWeldsToClosedMesh() {
    const AnalyzedMesh a = analyze(makeCubeSoup());
    const MeshReport& r = a.report;
    CHECK(r.fileVertices == 36);
    CHECK(r.weldedVertices == 8);
    CHECK(r.boundaryEdges == 0);
    CHECK(r.closedManifold);
    CHECK_NEAR(r.volume, 1.0, 1e-9);
}

static void testMissingFaceIsReportedAsHole() {
    Mesh m = makeCube();
    m.indices.erase(m.indices.begin() + 6, m.indices.begin() + 12);  // drop the top quad
    const MeshReport r = analyze(m).report;
    CHECK(r.boundaryEdges == 4);
    CHECK(r.holeLoops == 1);
    CHECK(r.openChains == 0);
    CHECK(!r.watertight);
    CHECK(r.holes.size() == 1);
    if (!r.holes.empty()) {
        CHECK(r.holes[0].edgeCount == 4);
        CHECK(r.holes[0].closed);
        CHECK_NEAR(r.holes[0].perimeter, 4.0, 1e-6);
        CHECK_NEAR(r.holes[0].center.z, 1.0, 1e-6);
    }
}

static void testTwoHoles() {
    Mesh m = makeCube();
    // remove top (indices 6..11) and bottom (0..5)
    m.indices.erase(m.indices.begin() + 6, m.indices.begin() + 12);
    m.indices.erase(m.indices.begin(), m.indices.begin() + 6);
    const MeshReport r = analyze(m).report;
    CHECK(r.boundaryEdges == 8);
    CHECK(r.holeLoops == 2);
    CHECK(r.eulerCharacteristic == 0);  // open tube
}

static void testFlippedNormals() {
    Mesh m = makeCube();
    for (size_t i = 0; i < m.indices.size(); i += 3) std::swap(m.indices[i + 1], m.indices[i + 2]);
    const MeshReport r = analyze(m).report;
    CHECK(r.closedManifold);  // still consistent, just inside-out
    CHECK(!r.normalsOutward);
    CHECK_NEAR(r.volume, -1.0, 1e-9);
}

static void testSingleFlippedTriangle() {
    Mesh m = makeCube();
    std::swap(m.indices[1], m.indices[2]);  // flip one bottom triangle
    const MeshReport r = analyze(m).report;
    CHECK(r.inconsistentEdges == 3);
    CHECK(!r.consistentWinding);
    CHECK(r.watertight);  // topologically closed, but not consistently oriented
    CHECK(!r.closedManifold);
}

static void testDegenerateTriangles() {
    Mesh m = makeCube();
    // collapsed triangle (two identical corners)
    m.indices.insert(m.indices.end(), {1, 1, 2});
    // zero-area triangle with three distinct but collinear corners, far from the cube
    const uint32_t base = static_cast<uint32_t>(m.positions.size());
    m.positions.push_back({5, 5, 5});
    m.positions.push_back({6, 5, 5});
    m.positions.push_back({7, 5, 5});
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
    // triangle with a NaN corner
    const uint32_t nanIdx = static_cast<uint32_t>(m.positions.size());
    m.positions.push_back({std::nanf(""), 0, 0});
    m.indices.insert(m.indices.end(), {0, 1, nanIdx});

    const AnalyzedMesh a = analyze(m);
    const MeshReport& r = a.report;
    CHECK(r.collapsedTriangles == 1);
    CHECK(r.zeroAreaTriangles == 1);
    CHECK(r.invalidTriangles == 1);
    CHECK(r.degenerateTriangles == 3);
    CHECK(r.nonFiniteVertices == 1);
    CHECK(a.overlays.degenerateMarkers.size() == 2);  // invalid one has no usable position
    CHECK(r.sliverTriangles == 0);
}

static void testNonManifoldEdge() {
    Mesh m;
    m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}};
    m.indices = {0, 1, 2, 0, 1, 3, 0, 1, 4};  // three fins on one edge
    const MeshReport r = analyze(m).report;
    CHECK(r.nonManifoldEdges == 1);
    CHECK(!r.watertight);
}

static void testOpenChainIsOneLoop() {
    // A fin glued onto one edge of the cube leaves two open edges that meet at the fin tip.
    // Whichever edge the walker starts from, they must be reported as ONE chain of two edges.
    Mesh m = makeCube();
    m.positions.push_back({0.5f, -1.0f, -1.0f});
    m.indices.insert(m.indices.end(), {0, 1, 8});
    const MeshReport r = analyze(m).report;
    CHECK(r.boundaryEdges == 2);
    CHECK(r.nonManifoldEdges == 1);
    CHECK(r.holeLoops == 1);
    CHECK(r.openChains == 1);
    if (!r.holes.empty()) {
        CHECK(r.holes[0].edgeCount == 2);
        CHECK(!r.holes[0].closed);
    }
}

static void testDuplicateTriangle() {
    Mesh m = makeCube();
    m.indices.insert(m.indices.end(), {0, 3, 2});  // same triangle again
    const MeshReport r = analyze(m).report;
    CHECK(r.duplicateTriangles == 1);
    CHECK(r.nonManifoldEdges > 0);
}

static void testSliver() {
    Mesh m;
    m.positions = {{0, 0, 0}, {10, 0, 0}, {10, 0.01f, 0}};  // smallest angle ~0.057 deg
    m.indices = {0, 1, 2};
    const MeshReport r = analyze(m).report;
    CHECK(r.sliverTriangles == 1);
    CHECK(r.degenerateTriangles == 0);
    CHECK(r.boundaryEdges == 3);
    CHECK(r.holeLoops == 1);
}

static void testLooseVerticesAndShells() {
    Mesh m = makeCube();
    m.positions.push_back({9, 9, 9});  // unreferenced
    // a second, separate tetra-like piece
    const uint32_t b = static_cast<uint32_t>(m.positions.size());
    m.positions.insert(m.positions.end(), {{20, 0, 0}, {21, 0, 0}, {20, 1, 0}, {20, 0, 1}});
    m.indices.insert(m.indices.end(), {b, b + 2, b + 1, b, b + 1, b + 3, b, b + 3, b + 2, b + 1, b + 2, b + 3});
    const MeshReport r = analyze(m).report;
    CHECK(r.looseVertices == 1);
    CHECK(r.shells == 2);
    CHECK(r.closedManifold);
    CHECK(r.eulerCharacteristic == 4);
    CHECK(r.genus == 0);
}

// Several separate unit cubes, spread out along X.
static Mesh makeCubes(int count) {
    const Mesh c = makeCube();
    Mesh m;
    for (int k = 0; k < count; ++k) {
        const uint32_t base = static_cast<uint32_t>(m.positions.size());
        for (const Vec3& p : c.positions) m.positions.push_back({p.x * (1.0f + 0.5f * static_cast<float>(k)) + 3.0f * static_cast<float>(k), p.y, p.z + 10.0f});
        for (uint32_t i : c.indices) m.indices.push_back(base + i);
    }
    return m;
}

static void testObjects() {
    const AnalyzedMesh a = analyze(makeCubes(3));
    CHECK(a.report.shells == 3);
    CHECK(a.objects.size() == 3);
    CHECK(a.render.objectIds.size() == a.render.positions.size());
    for (const ObjectInfo& o : a.objects) CHECK(o.triangles == 12);
    // every vertex belongs to the object whose bounds contain it
    for (size_t v = 0; v < a.render.positions.size(); ++v) {
        const AABB& b = a.objects[a.render.objectIds[v]].bounds;
        const Vec3 p = a.render.positions[v];
        CHECK(p.x >= b.min.x - 1e-5f && p.x <= b.max.x + 1e-5f);
    }
    // a hole belongs to the object that has it
    Mesh open = makeCubes(2);
    open.indices.erase(open.indices.begin() + 36 + 6, open.indices.begin() + 36 + 12);  // top quad of the 2nd cube
    const AnalyzedMesh h = analyze(open);
    CHECK(h.report.holes.size() == 1);
    if (!h.report.holes.empty()) CHECK(h.report.holes[0].object == 1);
    // a degenerate triangle's marker belongs to its object
    Mesh deg = makeCubes(2);
    deg.indices.insert(deg.indices.end(), {8, 8, 9});  // collapsed triangle on the 2nd cube
    const AnalyzedMesh d = analyze(deg);
    CHECK(d.overlays.degenerateMarkers.size() == 1 && d.overlays.degenerateMarkerObjects.size() == 1);
    if (!d.overlays.degenerateMarkerObjects.empty()) CHECK(d.overlays.degenerateMarkerObjects[0] == 1);
    // too many pieces: not separately movable
    Mesh soup;
    for (size_t k = 0; k < kMaxObjects + 5; ++k) {
        const float x = static_cast<float>(k) * 2.0f;
        const uint32_t b = static_cast<uint32_t>(soup.positions.size());
        soup.positions.insert(soup.positions.end(), {{x, 0, 0}, {x + 1, 0, 0}, {x, 1, 0}});
        soup.indices.insert(soup.indices.end(), {b, b + 1, b + 2});
    }
    const AnalyzedMesh s = analyze(soup);
    CHECK(s.report.shells == kMaxObjects + 5);
    CHECK(s.objects.empty());
}

static void testArrange() {
    // objects of different sizes, all overlapping each other and floating at different heights
    std::vector<AABB> boxes;
    for (int i = 0; i < 9; ++i) {
        AABB b;
        const float s = 1.0f + 0.7f * static_cast<float>(i % 4);
        b.expand({0.0f, 0.0f, 2.0f * static_cast<float>(i)});
        b.expand({s, s * 1.5f, 2.0f * static_cast<float>(i) + 1.0f});
        boxes.push_back(b);
    }
    for (int up : {2, 1}) {
        const auto off = arrangeObjects(boxes, up);
        CHECK(off.size() == boxes.size());
        const int gv = up == 2 ? 1 : 2;
        auto c = [](const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); };
        // use a rotated copy of the boxes for the Y-up case so the data really is "Y up"
        std::vector<AABB> bx;
        for (const AABB& b : boxes) {
            AABB r;
            r.expand(up == 2 ? b.min : Vec3{b.min.x, b.min.z, b.min.y});
            r.expand(up == 2 ? b.max : Vec3{b.max.x, b.max.z, b.max.y});
            bx.push_back(r);
        }
        const auto offs = arrangeObjects(bx, up);
        float level = 1e30f, lo = 1e30f, hi = -1e30f;
        for (size_t i = 0; i < bx.size(); ++i) {
            AABB m;
            m.expand(bx[i].min + offs[i]);
            m.expand(bx[i].max + offs[i]);
            level = std::min(level, c(m.min, up));
            CHECK_NEAR(c(m.min, up), c(bx[0].min + offs[0], up), 1e-4);  // everything rests on the same level
            lo = std::min(lo, c(m.min, 0));
            hi = std::max(hi, c(m.max, 0));
            for (size_t j = i + 1; j < bx.size(); ++j) {  // no two footprints overlap
                AABB n;
                n.expand(bx[j].min + offs[j]);
                n.expand(bx[j].max + offs[j]);
                const bool overlapU = m.min.x < n.max.x && n.min.x < m.max.x;
                const bool overlapV = c(m.min, gv) < c(n.max, gv) && c(n.min, gv) < c(m.max, gv);
                CHECK(!(overlapU && overlapV));
            }
        }
        CHECK(hi - lo > 0);
        (void)off;
    }
    CHECK(arrangeObjects({}, 2).empty());
    // a single object only drops to the level and stays centred
    AABB one;
    one.expand({5, 5, 5});
    one.expand({7, 8, 9});
    const auto single = arrangeObjects({one}, 2);
    CHECK_NEAR(single[0].x, 0.0, 1e-5);
    CHECK_NEAR(single[0].y, 0.0, 1e-5);
    CHECK_NEAR(single[0].z, 0.0, 1e-5);
}

static void testPicking() {
    const AnalyzedMesh a = analyze(makeCubes(2));  // cube 0: x 0..1, cube 1: x 3..4.5; both z 10..11
    std::vector<Vec3> offs(2);
    float dist = 0;
    // ray straight down +X... from the side at z=10.5, y=0.5
    CHECK(pickObject(a, offs, {-5, 0.5f, 10.5f}, {1, 0, 0}, &dist) == 0);
    CHECK_NEAR(dist, 5.0, 1e-4);
    CHECK(pickObject(a, offs, {2, 0.5f, 20.0f}, {0, 0, -1}) == -1);       // between the cubes
    CHECK(pickObject(a, offs, {3.2f, 0.5f, 20.0f}, {0, 0, -1}) == 1);      // from above, onto cube 1
    CHECK(pickObject(a, offs, {6, 0.5f, 10.5f}, {1, 0, 0}) == -1);         // pointing away
    // moving an object moves what can be picked
    offs[1] = {0, 0, 50};
    CHECK(pickObject(a, offs, {3.2f, 0.5f, 100.0f}, {0, 0, -1}) == 1);
    CHECK(pickObject(a, offs, {3.2f, 0.5f, 20.0f}, {0, 0, -1}) == -1);
    // the nearer of two overlapping objects wins: cube 1 (x 3..4.5) moved by -3.5 spans x -0.5..1.0
    offs[1] = {-3.5f, 0, 0};
    offs[0] = {0, 0, 0};
    CHECK(pickObject(a, offs, {-5, 0.5f, 10.5f}, {1, 0, 0}, &dist) == 1);
    CHECK_NEAR(dist, 4.5, 1e-4);
}

static void testGizmoMath() {
    float t = 0;
    // ray along +Z at x=2 passes the X axis at x=2
    CHECK(rayLineParameter({2, 0, -5}, {0, 0, 1}, {0, 0, 0}, {1, 0, 0}, t));
    CHECK_NEAR(t, 2.0, 1e-5);
    // along Y axis through (1,1,1): ray from above at z...
    CHECK(rayLineParameter({1, 1, 10}, {0, 0, -1}, {1, 0, 1}, {0, 1, 0}, t));
    CHECK_NEAR(t, 1.0, 1e-5);
    CHECK(!rayLineParameter({0, 0, 5}, {1, 0, 0}, {0, 0, 0}, {1, 0, 0}, t));  // parallel
    Vec3 hit;
    CHECK(rayPlaneIntersection({1, 2, 10}, {0, 0, -1}, {0, 0, 3}, {0, 0, 1}, hit));
    CHECK_NEAR(hit.x, 1.0, 1e-5);
    CHECK_NEAR(hit.z, 3.0, 1e-5);
    CHECK(!rayPlaneIntersection({1, 2, 10}, {1, 0, 0}, {0, 0, 3}, {0, 0, 1}, hit));   // parallel
    CHECK(!rayPlaneIntersection({1, 2, 10}, {0, 0, 1}, {0, 0, 3}, {0, 0, 1}, hit));   // plane is behind
}

static void testEmptyAndTiny() {
    Mesh empty;
    const MeshReport r0 = analyze(empty).report;
    CHECK(r0.triangles == 0 && !r0.watertight);

    Mesh pointsOnly;
    pointsOnly.positions = {{1, 2, 3}};
    const MeshReport r1 = analyze(pointsOnly).report;
    CHECK(r1.triangles == 0 && r1.looseVertices == 1);

    Mesh allSame;
    allSame.positions = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
    allSame.indices = {0, 1, 2};
    const MeshReport r2 = analyze(allSame).report;
    CHECK(r2.collapsedTriangles == 1);
}

// ---------------------------------------------------------------- loader tests

namespace fs = std::filesystem;

static fs::path tempDir() {
    fs::path d = fs::temp_directory_path() / "meshinspector_tests";
    fs::create_directories(d);
    return d;
}

static void writeText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
}
static void writeBytes(const fs::path& p, const std::vector<unsigned char>& b) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

template <class T>
static void put(std::vector<unsigned char>& out, T v, bool bigEndian = false) {
    unsigned char b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));  // host is little-endian on every supported platform
    if (bigEndian) std::reverse(b, b + sizeof(T));
    out.insert(out.end(), b, b + sizeof(T));
}

static Mesh loadOrFail(const fs::path& p) {
    Mesh m;
    std::string err;
    const bool ok = loadMeshFile(p, m, err);
    if (!ok) std::printf("    load error for %s: %s\n", p.string().c_str(), err.c_str());
    CHECK(ok);
    return m;
}

static void expectCube(const Mesh& m, const char* what) {
    const AnalyzedMesh a = analyze(m);
    if (!a.report.closedManifold || a.report.triangles != 12 || a.report.weldedVertices != 8) {
        std::printf("    %s: tris=%zu verts=%zu boundary=%zu\n", what, a.report.triangles, a.report.weldedVertices,
                    a.report.boundaryEdges);
    }
    CHECK(a.report.closedManifold);
    CHECK(a.report.triangles == 12);
    CHECK(a.report.weldedVertices == 8);
    CHECK_NEAR(a.report.volume, 1.0, 1e-6);
}

static void testObj() {
    // quads, negative indices, v/vt/vn syntax, comments, CRLF line endings
    std::string s = "# cube\r\n";
    const Mesh c = makeCube();
    for (const Vec3& p : c.positions) s += "v " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + "\r\n";
    s += "vt 0 0\nvn 0 0 1\n";
    s += "f 1/1/1 4/1/1 3/1/1 2/1/1\n";      // bottom quad  (0,3,2,1)
    s += "f 5//1 6//1 7//1 8//1\n";          // top quad     (4,5,6,7)
    s += "f 1 2 6 5\n";                      // front quad   (0,1,5,4)
    s += "f 3 4 8 7\n";                      // back quad    (2,3,7,6)
    s += "f 1 5 8 4\n";                      // left quad    (0,4,7,3)
    s += "f -7 -6 -2 -3\n";                  // right quad   (1,2,6,5) with negative indices
    const fs::path p = tempDir() / "cube.obj";
    writeText(p, s);
    const Mesh m = loadOrFail(p);
    CHECK(m.format == "Wavefront OBJ");
    expectCube(m, "obj");

    writeText(tempDir() / "bad.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\nf 1 2 3\n");
    const Mesh b = loadOrFail(tempDir() / "bad.obj");
    CHECK(b.indices.size() == 3);
    CHECK(!b.warnings.empty());
}

static void testStlAscii() {
    const Mesh c = makeCubeSoup();
    std::string s = "solid cube\n";
    for (size_t t = 0; t < c.indices.size(); t += 3) {
        s += "  facet normal 0 0 0\n    outer loop\n";
        for (int k = 0; k < 3; ++k) {
            const Vec3& v = c.positions[c.indices[t + static_cast<size_t>(k)]];
            s += "      vertex " + std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z) + "\n";
        }
        s += "    endloop\n  endfacet\n";
    }
    s += "endsolid cube\n";
    const fs::path p = tempDir() / "cube_ascii.stl";
    writeText(p, s);
    const Mesh m = loadOrFail(p);
    CHECK(m.format == "ASCII STL");
    expectCube(m, "ascii stl");
}

static void testStlBinary() {
    const Mesh c = makeCubeSoup();
    std::vector<unsigned char> b(80, 0);
    // a header that starts with "solid" must not fool the detection
    std::memcpy(b.data(), "solid exported by test", 22);
    put<uint32_t>(b, static_cast<uint32_t>(c.indices.size() / 3));
    for (size_t t = 0; t < c.indices.size(); t += 3) {
        for (int i = 0; i < 3; ++i) put<float>(b, 0.0f);  // normal
        for (int k = 0; k < 3; ++k) {
            const Vec3& v = c.positions[c.indices[t + static_cast<size_t>(k)]];
            put<float>(b, v.x);
            put<float>(b, v.y);
            put<float>(b, v.z);
        }
        put<uint16_t>(b, 0);
    }
    const fs::path p = tempDir() / "cube_bin.stl";
    writeBytes(p, b);
    const Mesh m = loadOrFail(p);
    CHECK(m.format == "Binary STL");
    expectCube(m, "binary stl");
}

static std::string plyHeader(const char* format, bool color) {
    std::string h = "ply\nformat ";
    h += format;
    h += " 1.0\ncomment test\nelement vertex 8\nproperty float x\nproperty float y\nproperty float z\n";
    if (color) h += "property uchar red\nproperty uchar green\nproperty uchar blue\n";
    h += "element face 6\nproperty list uchar int vertex_indices\nend_header\n";
    return h;
}

static void testPlyAscii() {
    const Mesh c = makeCube();
    std::string s = plyHeader("ascii", true);
    for (const Vec3& p : c.positions) s += std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + " 255 128 0\n";
    s += "4 0 3 2 1\n4 4 5 6 7\n4 0 1 5 4\n4 2 3 7 6\n4 0 4 7 3\n4 1 2 6 5\n";
    const fs::path p = tempDir() / "cube_ascii.ply";
    writeText(p, s);
    const Mesh m = loadOrFail(p);
    CHECK(m.format == "ASCII PLY");
    CHECK(m.colors.size() == 8);
    if (m.colors.size() == 8) CHECK_NEAR(m.colors[0].y, 128.0 / 255.0, 1e-4);
    expectCube(m, "ascii ply");
}

static void testPlyBinary(bool bigEndian) {
    const Mesh c = makeCube();
    std::vector<unsigned char> b;
    const std::string h = plyHeader(bigEndian ? "binary_big_endian" : "binary_little_endian", false);
    b.insert(b.end(), h.begin(), h.end());
    for (const Vec3& p : c.positions) {
        put<float>(b, p.x, bigEndian);
        put<float>(b, p.y, bigEndian);
        put<float>(b, p.z, bigEndian);
    }
    const int quads[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 4, 7, 3}, {1, 2, 6, 5}};
    for (const auto& q : quads) {
        put<uint8_t>(b, 4);
        for (int v : q) put<int32_t>(b, v, bigEndian);
    }
    const fs::path p = tempDir() / (bigEndian ? "cube_be.ply" : "cube_le.ply");
    writeBytes(p, b);
    const Mesh m = loadOrFail(p);
    CHECK(m.format == "Binary PLY");
    expectCube(m, bigEndian ? "binary BE ply" : "binary LE ply");
}

static void testOff() {
    const Mesh c = makeCube();
    std::string s = "OFF\n# comment\n8 6 12\n";
    for (const Vec3& p : c.positions) s += std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + "\n";
    s += "4 0 3 2 1\n4 4 5 6 7\n4 0 1 5 4\n4 2 3 7 6\n4 0 4 7 3\n4 1 2 6 5\n";
    const fs::path p = tempDir() / "cube.off";
    writeText(p, s);
    expectCube(loadOrFail(p), "off");
}

static void testGlb() {
    const Mesh c = makeCube();
    std::vector<unsigned char> bin;
    for (const Vec3& p : c.positions) {
        put<float>(bin, p.x);
        put<float>(bin, p.y);
        put<float>(bin, p.z);
    }
    for (uint32_t i : c.indices) put<uint16_t>(bin, static_cast<uint16_t>(i));
    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"mesh\":0,\"translation\":[10,0,0]}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,1]},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":36,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":96,\"target\":34962},"
        "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":72,\"target\":34963}],"
        "\"buffers\":[{\"byteLength\":168}]}";
    while (json.size() % 4) json += ' ';
    while (bin.size() % 4) bin.push_back(0);
    std::vector<unsigned char> glb;
    put<uint32_t>(glb, 0x46546C67u);
    put<uint32_t>(glb, 2u);
    put<uint32_t>(glb, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    put<uint32_t>(glb, static_cast<uint32_t>(json.size()));
    put<uint32_t>(glb, 0x4E4F534Au);
    glb.insert(glb.end(), json.begin(), json.end());
    put<uint32_t>(glb, static_cast<uint32_t>(bin.size()));
    put<uint32_t>(glb, 0x004E4942u);
    glb.insert(glb.end(), bin.begin(), bin.end());

    const fs::path p = tempDir() / "cube.glb";
    writeBytes(p, glb);
    const Mesh m = loadOrFail(p);
    CHECK(m.format == "glTF Binary (GLB)");
    expectCube(m, "glb");
    // node translation must have been applied
    const AnalyzedMesh a = analyze(m);
    CHECK_NEAR(a.report.bounds.min.x, 10.0, 1e-5);
}

static void testThreeMf() {
    const fs::path dir = TEST_DATA_DIR;
    // deflate-compressed and stored archives must give the same result; the build item moves it +10 in X
    for (const char* name : {"cube.3mf", "cube_stored.3mf"}) {
        const Mesh m = loadOrFail(dir / name);
        CHECK(m.format == "3MF");
        expectCube(m, name);
        CHECK_NEAR(analyze(m).report.bounds.min.x, 10.0, 1e-5);
    }
    // multi-file package: the cube is a component placed twice, once mirrored
    const Mesh c = loadOrFail(dir / "components.3mf");
    const MeshReport r = analyze(c).report;
    CHECK(r.triangles == 24);
    CHECK(r.shells == 2);
    CHECK(r.closedManifold);   // the mirrored copy must have had its winding corrected
    CHECK(r.normalsOutward);
    CHECK_NEAR(r.volume, 2.0, 1e-6);
    CHECK_NEAR(r.bounds.min.x, -6.0, 1e-5);
    CHECK_NEAR(r.bounds.max.x, 1.0, 1e-5);
    // larger file: exercises dynamic-Huffman deflate blocks
    const Mesh s = loadOrFail(fs::path(SAMPLES_DIR) / "icosphere_clean.3mf");
    const MeshReport sr = analyze(s).report;
    CHECK(sr.triangles == 1280);
    CHECK(sr.weldedVertices == 642);
    CHECK(sr.closedManifold);
}

static void testThreeMfCorruption() {
    const fs::path dir = TEST_DATA_DIR;
    std::ifstream f(fs::path(SAMPLES_DIR) / "icosphere_clean.3mf", std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(bytes.size() > 1000);
    // truncated archive
    std::vector<unsigned char> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
    writeBytes(tempDir() / "cut.3mf", cut);
    Mesh m;
    std::string err;
    CHECK(!loadMeshFile(tempDir() / "cut.3mf", m, err));
    CHECK(!err.empty());
    // flipped bytes inside the compressed data must fail cleanly (or at worst load), never crash
    for (size_t pos = 100; pos < bytes.size() - 200; pos += 997) {
        std::vector<unsigned char> bad(bytes.begin(), bytes.end());
        bad[pos] ^= 0x5A;
        writeBytes(tempDir() / "bad.3mf", bad);
        Mesh mm;
        std::string e2;
        loadMeshFile(tempDir() / "bad.3mf", mm, e2);
    }
    // not a zip at all
    writeText(tempDir() / "text.3mf", "this is plain text, not a zip archive at all, but long enough to be inspected");
    err.clear();
    CHECK(!loadMeshFile(tempDir() / "text.3mf", m, err));
    CHECK(!err.empty());
    (void)dir;
}

static void testSettingsRoundTrip() {
    Settings a;
    a.view.wireframe = true;
    a.view.zUp = false;
    a.view.vsync = false;
    a.view.autoReload = true;
    a.view.showSlivers = false;
    a.view.background[0] = 0.9f;
    a.view.meshColor[1] = 0.25f;
    a.view.backColor[2] = 0.0f;
    a.view.weldExponent = 7.5f;
    a.view.analysis.sliverAngleDeg = 2.5;
    a.window.width = 1111;
    a.window.height = 777;
    a.window.maximized = true;
    a.view.hideUi = true;  // transient: must NOT be saved

    Settings b;
    parseSettings(serializeSettings(a), b);
    CHECK(b.view.wireframe && !b.view.zUp && !b.view.vsync && b.view.autoReload && !b.view.showSlivers);
    CHECK_NEAR(b.view.background[0], 0.9, 1e-4);
    CHECK_NEAR(b.view.meshColor[1], 0.25, 1e-4);
    CHECK_NEAR(b.view.backColor[2], 0.0, 1e-4);
    CHECK_NEAR(b.view.weldExponent, 7.5, 1e-4);
    CHECK_NEAR(b.view.analysis.weldTolerance, std::pow(10.0, -7.5), 1e-12);
    CHECK_NEAR(b.view.analysis.sliverAngleDeg, 2.5, 1e-4);
    CHECK(b.window.width == 1111 && b.window.height == 777 && b.window.maximized);
    CHECK(!b.view.hideUi);
    // untouched settings keep their defaults
    CHECK(b.view.flatShading && b.view.showGrid && b.view.showReport);

    // file round trip, replacing an existing file
    const fs::path file = tempDir() / "cfg" / "settings.ini";
    fs::remove_all(file.parent_path());
    CHECK(saveSettingsFile(file, a));
    a.view.wireframe = false;
    CHECK(saveSettingsFile(file, a));
    Settings c;
    CHECK(loadSettingsFile(file, c));
    CHECK(!c.view.wireframe && !c.view.zUp);
    CHECK(!fs::exists(file.string() + ".tmp"));
    Settings none;
    CHECK(!loadSettingsFile(tempDir() / "cfg" / "missing.ini", none));
}

static void testSettingsAreLenient() {
    Settings s;
    parseSettings(
        "# comment\n"
        "garbage line without equals\n"
        "unknown_key = 5\n"
        "show_grid = false\n"
        "wireframe = maybe\n"             // not a boolean: ignored
        "background_color = 2 -1 0.5\n"   // clamped to 0..1
        "surface_color = 0.5 0.5\n"       // too few numbers: ignored
        "weld_tolerance_exponent = 99\n"  // clamped to the valid range
        "sliver_angle_degrees = nan\n"    // not finite: ignored
        "window_width = -50\n"            // clamped
        "\r\n   vsync   =   false   \r\n",
        s);
    CHECK(!s.view.showGrid);
    CHECK(!s.view.wireframe);
    CHECK_NEAR(s.view.background[0], 1.0, 1e-6);
    CHECK_NEAR(s.view.background[1], 0.0, 1e-6);
    CHECK_NEAR(s.view.background[2], 0.5, 1e-6);
    CHECK_NEAR(s.view.meshColor[0], ViewSettings{}.meshColor[0], 1e-6);
    CHECK_NEAR(s.view.weldExponent, 9.0, 1e-6);
    CHECK_NEAR(s.view.analysis.sliverAngleDeg, 1.0, 1e-6);
    CHECK(s.window.width == 0);
    CHECK(!s.view.vsync);
    // binary junk must not crash
    std::string junk;
    for (int i = 0; i < 2000; ++i) junk += static_cast<char>(i * 37);
    parseSettings(junk, s);
}

static void testErrors() {
    Mesh m;
    std::string err;
    CHECK(!loadMeshFile(tempDir() / "does_not_exist.stl", m, err));
    CHECK(!err.empty());
    writeText(tempDir() / "garbage.ply", "this is not a ply file");
    err.clear();
    CHECK(!loadMeshFile(tempDir() / "garbage.ply", m, err));
    CHECK(!err.empty());
    writeText(tempDir() / "what.xyz", "hello");
    err.clear();
    CHECK(!loadMeshFile(tempDir() / "what.xyz", m, err));
    CHECK(!err.empty());
    // truncated binary PLY must fail cleanly, not crash
    std::vector<unsigned char> b;
    const std::string h = plyHeader("binary_little_endian", false);
    b.insert(b.end(), h.begin(), h.end());
    put<float>(b, 1.0f);
    writeBytes(tempDir() / "trunc.ply", b);
    err.clear();
    CHECK(!loadMeshFile(tempDir() / "trunc.ply", m, err));
    CHECK(!err.empty());
}

int main() {
    struct Test {
        const char* name;
        std::function<void()> fn;
    };
    const Test tests[] = {
        {"clean cube", testCleanCube},
        {"cube soup welds to closed mesh", testCubeSoupWeldsToClosedMesh},
        {"missing face -> hole", testMissingFaceIsReportedAsHole},
        {"two holes", testTwoHoles},
        {"flipped normals", testFlippedNormals},
        {"single flipped triangle", testSingleFlippedTriangle},
        {"degenerate triangles", testDegenerateTriangles},
        {"non-manifold edge", testNonManifoldEdge},
        {"open chain is one loop", testOpenChainIsOneLoop},
        {"duplicate triangle", testDuplicateTriangle},
        {"sliver triangle", testSliver},
        {"loose vertices + shells", testLooseVerticesAndShells},
        {"objects (shells)", testObjects},
        {"arrange objects", testArrange},
        {"picking objects", testPicking},
        {"gizmo ray math", testGizmoMath},
        {"empty / tiny meshes", testEmptyAndTiny},
        {"OBJ loader", testObj},
        {"ASCII STL loader", testStlAscii},
        {"binary STL loader", testStlBinary},
        {"ASCII PLY loader", testPlyAscii},
        {"binary PLY (little endian)", [] { testPlyBinary(false); }},
        {"binary PLY (big endian)", [] { testPlyBinary(true); }},
        {"OFF loader", testOff},
        {"GLB loader", testGlb},
        {"3MF loader", testThreeMf},
        {"3MF corrupt files", testThreeMfCorruption},
        {"settings round trip", testSettingsRoundTrip},
        {"settings parser is lenient", testSettingsAreLenient},
        {"loader error handling", testErrors},
    };
    for (const Test& t : tests) {
        const int before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
