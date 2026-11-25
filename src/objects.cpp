// SPDX-License-Identifier: Apache-2.0
#include "objects.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mi {

namespace {

float comp(const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
void setComp(Vec3& v, int i, float value) { (i == 0 ? v.x : (i == 1 ? v.y : v.z)) = value; }

// Slab test: does the ray touch the box?
bool rayHitsBox(const Vec3& o, const Vec3& d, const Vec3& lo, const Vec3& hi) {
    float tmin = 0.0f, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        const float oi = comp(o, i), di = comp(d, i);
        if (std::fabs(di) < 1e-20f) {
            if (oi < comp(lo, i) || oi > comp(hi, i)) return false;
        } else {
            float t0 = (comp(lo, i) - oi) / di, t1 = (comp(hi, i) - oi) / di;
            if (t0 > t1) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmin > tmax) return false;
        }
    }
    return true;
}

}  // namespace

std::vector<Vec3> arrangeObjects(const std::vector<AABB>& bounds, int upAxis) {
    const size_t n = bounds.size();
    std::vector<Vec3> offsets(n);
    if (n == 0) return offsets;

    const int up = upAxis == 1 ? 1 : 2;
    const int gu = 0;
    const int gv = up == 2 ? 1 : 2;  // the two ground axes

    struct Item {
        size_t id;
        float w, h;
    };
    std::vector<Item> items;
    float levelMin = 1e30f, uLo = 1e30f, uHi = -1e30f, vLo = 1e30f, vHi = -1e30f, sizeSum = 0;
    for (size_t i = 0; i < n; ++i) {
        const AABB& b = bounds[i];
        if (!b.valid()) continue;
        items.push_back({i, comp(b.max, gu) - comp(b.min, gu), comp(b.max, gv) - comp(b.min, gv)});
        levelMin = std::min(levelMin, comp(b.min, up));
        uLo = std::min(uLo, comp(b.min, gu));
        uHi = std::max(uHi, comp(b.max, gu));
        vLo = std::min(vLo, comp(b.min, gv));
        vHi = std::max(vHi, comp(b.max, gv));
        sizeSum += std::max(items.back().w, items.back().h);
    }
    if (items.empty()) return offsets;

    const float gap = std::max(0.1f * sizeSum / static_cast<float>(items.size()), 1e-6f);
    float area = 0, widest = 0;
    for (const Item& it : items) {
        area += (it.w + gap) * (it.h + gap);
        widest = std::max(widest, it.w);
    }
    const float rowWidth = std::max(std::sqrt(area) * 1.25f, widest);

    // Shelf packing, tallest first.
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.h > b.h; });
    std::vector<float> px(n, 0), py(n, 0);
    float x = 0, y = 0, rowH = 0, usedW = 0;
    for (const Item& it : items) {
        if (x > 0 && x + it.w > rowWidth) {
            x = 0;
            y += rowH + gap;
            rowH = 0;
        }
        px[it.id] = x;
        py[it.id] = y;
        x += it.w + gap;
        usedW = std::max(usedW, x - gap);
        rowH = std::max(rowH, it.h);
    }
    const float usedH = y + rowH;

    // Keep the group where it was (centre of the original footprint).
    const float originU = (uLo + uHi) * 0.5f - usedW * 0.5f;
    const float originV = (vLo + vHi) * 0.5f - usedH * 0.5f;
    for (const Item& it : items) {
        const AABB& b = bounds[it.id];
        setComp(offsets[it.id], gu, originU + px[it.id] - comp(b.min, gu));
        setComp(offsets[it.id], gv, originV + py[it.id] - comp(b.min, gv));
        setComp(offsets[it.id], up, levelMin - comp(b.min, up));
    }
    return offsets;
}

int pickObject(const AnalyzedMesh& mesh, const std::vector<Vec3>& offsets, const Vec3& o, const Vec3& d, float* distance) {
    const size_t objectCount = mesh.objects.size();
    const RenderMesh& rm = mesh.render;
    if (objectCount == 0 || offsets.size() < objectCount || rm.objectIds.size() != rm.positions.size()) return -1;

    std::vector<uint8_t> candidate(objectCount, 0);
    bool any = false;
    for (size_t i = 0; i < objectCount; ++i) {
        const AABB& b = mesh.objects[i].bounds;
        if (!b.valid()) continue;
        if (rayHitsBox(o, d, b.min + offsets[i], b.max + offsets[i])) candidate[i] = any = 1;
    }
    if (!any) return -1;

    float best = 1e30f;
    int bestObject = -1;
    for (size_t t = 0; t + 2 < rm.indices.size(); t += 3) {
        const uint32_t i0 = rm.indices[t], i1 = rm.indices[t + 1], i2 = rm.indices[t + 2];
        const uint32_t obj = rm.objectIds[i0];
        if (!candidate[obj]) continue;
        const Vec3 off = offsets[obj];
        const Vec3 p0 = rm.positions[i0] + off;
        const Vec3 e1 = rm.positions[i1] + off - p0, e2 = rm.positions[i2] + off - p0;
        // Moeller-Trumbore, double sided
        const Vec3 pv = cross(d, e2);
        const float det = dot(e1, pv);
        if (std::fabs(det) < 1e-20f) continue;
        const float inv = 1.0f / det;
        const Vec3 tv = o - p0;
        const float u = dot(tv, pv) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        const Vec3 qv = cross(tv, e1);
        const float v = dot(d, qv) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float dist = dot(e2, qv) * inv;
        if (dist > 0.0f && dist < best) {
            best = dist;
            bestObject = static_cast<int>(obj);
        }
    }
    if (bestObject >= 0 && distance) *distance = best;
    return bestObject;
}

bool rayLineParameter(const Vec3& o, const Vec3& d, const Vec3& p, const Vec3& a, float& t) {
    // minimise |o + s d - (p + t a)| for unit d and a
    const Vec3 w0 = o - p;
    const float b = dot(d, a), dd = dot(d, w0), e = dot(a, w0);
    const float denom = 1.0f - b * b;
    if (denom < 1e-4f) return false;
    const float s = (b * e - dd) / denom;
    t = e + s * b;
    return true;
}

bool rayPlaneIntersection(const Vec3& o, const Vec3& d, const Vec3& p, const Vec3& n, Vec3& hit) {
    const float denom = dot(d, n);
    if (std::fabs(denom) < 1e-5f) return false;
    const float t = dot(p - o, n) / denom;
    if (t < 0.0f) return false;
    hit = o + d * t;
    return true;
}

}  // namespace mi
