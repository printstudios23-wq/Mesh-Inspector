// SPDX-License-Identifier: Apache-2.0
#include "analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace mi {

namespace {

constexpr uint32_t kInvalid = 0xFFFFFFFFu;
constexpr size_t kMaxStoredHoles = 5000;

struct WeldRec {
    int32_t x, y, z;
    uint32_t idx;
};

struct EdgeRec {
    uint64_t key;    // (min vertex << 32) | max vertex
    uint32_t face;
    uint32_t fwd;    // 1 if the face traverses the edge from the smaller to the larger vertex id
};

struct UnionFind {
    std::vector<uint32_t> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    uint32_t find(uint32_t a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    }
    void unite(uint32_t a, uint32_t b) {
        a = find(a);
        b = find(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
};

inline uint64_t edgeKey(uint32_t a, uint32_t b) {
    return a < b ? (static_cast<uint64_t>(a) << 32) | b : (static_cast<uint64_t>(b) << 32) | a;
}

}  // namespace

AnalyzedMesh analyzeMesh(const Mesh& mesh, const AnalysisSettings& settings) {
    AnalyzedMesh result;
    MeshReport& rep = result.report;
    RenderMesh& rm = result.render;
    Overlays& ov = result.overlays;

    const size_t nVerts = mesh.positions.size();
    const size_t nTris = mesh.triangleCount();
    rep.fileVertices = nVerts;
    rep.fileTriangles = nTris;

    // ------------------------------------------------------------------ bounds
    AABB bounds;
    for (const Vec3& p : mesh.positions) {
        if (isFinite(p)) bounds.expand(p);
        else ++rep.nonFiniteVertices;
    }
    rep.bounds = bounds;
    if (!bounds.valid()) return result;  // nothing usable

    double diag = static_cast<double>(length(Vec3d(bounds.size())));
    if (!(diag > 0)) diag = 1.0;
    const double tol = std::clamp(settings.weldTolerance, 1e-9, 1e-2);
    const double eps = diag * tol;
    rep.weldDistance = eps;
    const Vec3d bmin(bounds.min);
    const Vec3d bcenter(bounds.center());

    // ------------------------------------------------------------------ weld
    std::vector<WeldRec> recs;
    recs.reserve(nVerts);
    for (size_t i = 0; i < nVerts; ++i) {
        const Vec3& p = mesh.positions[i];
        if (!isFinite(p)) continue;
        recs.push_back({static_cast<int32_t>(std::floor((static_cast<double>(p.x) - bmin.x) / eps)),
                        static_cast<int32_t>(std::floor((static_cast<double>(p.y) - bmin.y) / eps)),
                        static_cast<int32_t>(std::floor((static_cast<double>(p.z) - bmin.z) / eps)), static_cast<uint32_t>(i)});
    }
    std::sort(recs.begin(), recs.end(), [](const WeldRec& a, const WeldRec& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        if (a.z != b.z) return a.z < b.z;
        return a.idx < b.idx;
    });

    std::vector<uint32_t> remap(nVerts, kInvalid);
    std::vector<uint32_t> firstOf;  // representative original vertex per welded vertex
    firstOf.reserve(recs.size());
    for (size_t k = 0; k < recs.size(); ++k) {
        const bool fresh = k == 0 || recs[k].x != recs[k - 1].x || recs[k].y != recs[k - 1].y || recs[k].z != recs[k - 1].z;
        if (fresh) firstOf.push_back(recs[k].idx);
        remap[recs[k].idx] = static_cast<uint32_t>(firstOf.size() - 1);
    }
    recs.clear();
    recs.shrink_to_fit();

    const size_t nWelded = firstOf.size();
    rep.weldedVertices = nWelded;
    rm.positions.resize(nWelded);
    for (size_t i = 0; i < nWelded; ++i) rm.positions[i] = mesh.positions[firstOf[i]];
    if (!mesh.colors.empty()) {
        rm.colors.resize(nWelded);
        for (size_t i = 0; i < nWelded; ++i) rm.colors[i] = mesh.colors[firstOf[i]];
    }

    // ------------------------------------------------------------------ triangles
    const double sinSliver = std::sin(settings.sliverAngleDeg * 3.14159265358979323846 / 180.0);
    std::vector<std::array<uint32_t, 3>> faces;  // valid (non-collapsed) triangles, welded ids
    faces.reserve(nTris);

    std::vector<Vec3d> normalAcc(nWelded);  // angle-weighted vertex normals
    double volume = 0, area = 0;

    auto addMarker = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        ov.degenerateMarkers.push_back((a + b + c) / 3.0f);
    };

    for (size_t t = 0; t < nTris; ++t) {
        const uint32_t o0 = mesh.indices[t * 3], o1 = mesh.indices[t * 3 + 1], o2 = mesh.indices[t * 3 + 2];
        if (o0 >= nVerts || o1 >= nVerts || o2 >= nVerts) {  // loaders guard against this; stay safe anyway
            ++rep.invalidTriangles;
            continue;
        }
        const uint32_t a = remap[o0], b = remap[o1], c = remap[o2];
        if (a == kInvalid || b == kInvalid || c == kInvalid) {
            ++rep.invalidTriangles;
            continue;
        }
        if (a == b || b == c || a == c) {
            ++rep.collapsedTriangles;
            addMarker(mesh.positions[o0], mesh.positions[o1], mesh.positions[o2]);
            continue;
        }

        const Vec3d pa(rm.positions[a]), pb(rm.positions[b]), pc(rm.positions[c]);
        const Vec3d e0 = pb - pa, e1 = pc - pb, e2 = pa - pc;
        const Vec3d n2 = cross(e0, pc - pa);
        const double area2 = length(n2);
        const double l0 = length(e0), l1 = length(e1), l2 = length(e2);
        const double longest = std::max({l0, l1, l2});

        if (longest <= 0 || area2 / longest <= eps) {
            // thickness (height over the longest edge) below the weld tolerance: effectively a line
            ++rep.zeroAreaTriangles;
            addMarker(rm.positions[a], rm.positions[b], rm.positions[c]);
            ov.degenerateEdges.insert(ov.degenerateEdges.end(), {a, b, b, c, c, a});
        } else {
            area += 0.5 * area2;
            // sin(smallest angle) = area2 / (product of the two longest edges)
            const double s1 = std::max({l0, l1, l2});
            const double s3 = std::min({l0, l1, l2});
            const double s2 = l0 + l1 + l2 - s1 - s3;
            if (area2 / (s1 * s2) < sinSliver) {
                ++rep.sliverTriangles;
                ov.sliverTriangles.insert(ov.sliverTriangles.end(), {a, b, c});
            }
            // angle-weighted smooth normals
            const Vec3d fn = n2 / area2;
            auto angle = [](const Vec3d& u, const Vec3d& v) {
                const double lu = length(u), lv = length(v);
                if (lu <= 0 || lv <= 0) return 0.0;
                return std::acos(std::clamp(dot(u, v) / (lu * lv), -1.0, 1.0));
            };
            normalAcc[a] += fn * angle(e0, -e2);
            normalAcc[b] += fn * angle(-e0, e1);
            normalAcc[c] += fn * angle(-e1, e2);
        }

        const Vec3d qa = pa - bcenter, qb = pb - bcenter, qc = pc - bcenter;
        volume += dot(qa, cross(qb, qc)) / 6.0;

        faces.push_back({a, b, c});
    }
    rep.degenerateTriangles = rep.collapsedTriangles + rep.zeroAreaTriangles + rep.invalidTriangles;
    rep.triangles = faces.size();
    rep.surfaceArea = area;
    rep.volume = volume;

    // render data
    rm.normals.resize(nWelded);
    for (size_t i = 0; i < nWelded; ++i) {
        const Vec3d n = normalize(normalAcc[i]);
        rm.normals[i] = (n.x == 0 && n.y == 0 && n.z == 0) ? Vec3{0, 1, 0} : Vec3(n);
    }
    rm.indices.reserve(faces.size() * 3);
    for (const auto& f : faces) rm.indices.insert(rm.indices.end(), {f[0], f[1], f[2]});

    // ------------------------------------------------------------------ duplicate triangles
    {
        std::vector<std::array<uint32_t, 3>> keys(faces);
        for (auto& k : keys) std::sort(k.begin(), k.end());
        std::sort(keys.begin(), keys.end());
        const size_t unique = static_cast<size_t>(std::unique(keys.begin(), keys.end()) - keys.begin());
        rep.duplicateTriangles = faces.size() - unique;
    }

    // ------------------------------------------------------------------ edges
    std::vector<EdgeRec> edges;
    edges.reserve(faces.size() * 3);
    for (uint32_t f = 0; f < faces.size(); ++f) {
        const auto& t = faces[f];
        for (int k = 0; k < 3; ++k) {
            const uint32_t u = t[k], v = t[(k + 1) % 3];
            edges.push_back({edgeKey(u, v), f, u < v ? 1u : 0u});
        }
    }
    std::sort(edges.begin(), edges.end(), [](const EdgeRec& a, const EdgeRec& b) { return a.key < b.key; });

    std::vector<std::pair<uint32_t, uint32_t>> boundary;  // endpoints of open edges
    for (size_t i = 0; i < edges.size();) {
        size_t j = i + 1;
        while (j < edges.size() && edges[j].key == edges[i].key) ++j;
        const size_t cnt = j - i;
        const uint32_t lo = static_cast<uint32_t>(edges[i].key >> 32);
        const uint32_t hi = static_cast<uint32_t>(edges[i].key & 0xFFFFFFFFu);
        ++rep.edges;
        if (cnt == 1) {
            ++rep.boundaryEdges;
            boundary.emplace_back(lo, hi);
            ov.boundaryEdges.insert(ov.boundaryEdges.end(), {lo, hi});
        } else if (cnt == 2) {
            if (edges[i].fwd == edges[i + 1].fwd) {
                ++rep.inconsistentEdges;
                ov.inconsistentEdges.insert(ov.inconsistentEdges.end(), {lo, hi});
            }
        } else {
            ++rep.nonManifoldEdges;
            ov.nonManifoldEdges.insert(ov.nonManifoldEdges.end(), {lo, hi});
        }
        i = j;
    }
    edges.clear();
    edges.shrink_to_fit();

    // ------------------------------------------------------------------ hole loops
    if (!boundary.empty()) {
        // adjacency: (vertex, edge index) sorted by vertex
        std::vector<std::pair<uint32_t, uint32_t>> adj;
        adj.reserve(boundary.size() * 2);
        for (uint32_t e = 0; e < boundary.size(); ++e) {
            adj.emplace_back(boundary[e].first, e);
            adj.emplace_back(boundary[e].second, e);
        }
        std::sort(adj.begin(), adj.end());
        std::vector<uint8_t> used(boundary.size(), 0);

        auto edgeLen = [&](uint32_t e) {
            return static_cast<double>(length(Vec3d(rm.positions[boundary[e].first]) - Vec3d(rm.positions[boundary[e].second])));
        };

        std::vector<HoleLoop> loops;
        for (uint32_t e0 = 0; e0 < boundary.size(); ++e0) {
            if (used[e0]) continue;
            used[e0] = 1;
            const uint32_t start = boundary[e0].first;
            const uint32_t first = boundary[e0].second;
            HoleLoop loop;
            loop.edgeCount = 1;
            loop.perimeter = edgeLen(e0);
            loop.closed = false;
            Vec3d sum = Vec3d(rm.positions[start]) + Vec3d(rm.positions[first]);
            std::vector<uint32_t> verts{start, first};

            // Follows unused open edges from `from` until the chain ends (or comes back to `stopAt`).
            auto walk = [&](uint32_t from, uint32_t stopAt) {
                uint32_t cur = from;
                while (true) {
                    auto it = std::lower_bound(adj.begin(), adj.end(), std::make_pair(cur, 0u));
                    uint32_t next = kInvalid;
                    for (; it != adj.end() && it->first == cur; ++it) {
                        if (!used[it->second]) {
                            next = it->second;
                            break;
                        }
                    }
                    if (next == kInvalid) return cur;
                    used[next] = 1;
                    ++loop.edgeCount;
                    loop.perimeter += edgeLen(next);
                    cur = boundary[next].first == cur ? boundary[next].second : boundary[next].first;
                    if (cur == stopAt) {
                        loop.closed = true;
                        return cur;
                    }
                    verts.push_back(cur);
                    sum += Vec3d(rm.positions[cur]);
                }
            };
            // Extend forwards; if the chain did not close, also extend backwards from its other end
            // (the first edge we picked may lie in the middle of an open chain).
            const uint32_t forwardEnd = walk(first, start);
            if (!loop.closed) walk(start, forwardEnd);

            const Vec3d center = sum / static_cast<double>(verts.size());
            loop.center = Vec3(center);
            double r2 = 0;
            for (uint32_t v : verts) {
                const Vec3d d = Vec3d(rm.positions[v]) - center;
                r2 = std::max(r2, dot(d, d));
            }
            loop.radius = static_cast<float>(std::sqrt(r2));
            loops.push_back(loop);
        }
        rep.holeLoops = loops.size();
        for (const HoleLoop& l : loops)
            if (!l.closed) ++rep.openChains;
        const size_t keep = std::min(loops.size(), kMaxStoredHoles);
        std::partial_sort(loops.begin(), loops.begin() + static_cast<std::ptrdiff_t>(keep), loops.end(),
                          [](const HoleLoop& a, const HoleLoop& b) { return a.perimeter > b.perimeter; });
        loops.resize(keep);
        rep.holes = std::move(loops);
    }

    // ------------------------------------------------------------------ shells & vertex usage
    {
        UnionFind uf(nWelded);
        std::vector<uint8_t> usedVertex(nWelded, 0);
        for (const auto& f : faces) {
            uf.unite(f[0], f[1]);
            uf.unite(f[0], f[2]);
            usedVertex[f[0]] = usedVertex[f[1]] = usedVertex[f[2]] = 1;
        }
        size_t used = 0;
        for (uint32_t v = 0; v < nWelded; ++v) {
            if (!usedVertex[v]) continue;
            ++used;
            if (uf.find(v) == v) ++rep.shells;
        }
        rep.looseVertices = nWelded - used;
        rep.eulerCharacteristic = static_cast<long long>(used) - static_cast<long long>(rep.edges) + static_cast<long long>(faces.size());
    }

    // ------------------------------------------------------------------ verdicts
    rep.watertight = !faces.empty() && rep.boundaryEdges == 0 && rep.nonManifoldEdges == 0;
    rep.consistentWinding = rep.inconsistentEdges == 0;
    rep.closedManifold = rep.watertight && rep.consistentWinding;
    rep.normalsOutward = rep.volume > 0;
    if (rep.closedManifold) rep.genus = (2 * static_cast<long long>(rep.shells) - rep.eulerCharacteristic) / 2;

    return result;
}

}  // namespace mi
