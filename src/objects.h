// SPDX-License-Identifier: Apache-2.0
// Working with the separate objects (connected pieces) of a mesh: arranging them like a slicer,
// picking one with a mouse ray, and the ray maths behind the move gizmo.
#pragma once

#include <vector>

#include "analysis.h"
#include "math3d.h"

namespace mi {

// Lays the objects out side by side on the "ground" (the plane perpendicular to `upAxis`,
// 1 = Y or 2 = Z) with gaps between them, centred where they were, all resting on the same
// level. Returns one translation per object, in the mesh's coordinates.
std::vector<Vec3> arrangeObjects(const std::vector<AABB>& bounds, int upAxis);

// Index of the object hit first by the ray (origin / unit direction in mesh coordinates, objects
// shifted by `offsets`), or -1. `distance` receives the hit distance if not null.
int pickObject(const AnalyzedMesh& mesh, const std::vector<Vec3>& offsets, const Vec3& origin, const Vec3& dir, float* distance = nullptr);

// Closest point between the ray and the infinite line through `point` along the unit vector `axis`:
// returns the parameter along the line. False if the two are (nearly) parallel.
bool rayLineParameter(const Vec3& origin, const Vec3& dir, const Vec3& point, const Vec3& axis, float& t);

// Intersection of the ray with a plane. False if parallel or the plane is behind the ray.
bool rayPlaneIntersection(const Vec3& origin, const Vec3& dir, const Vec3& planePoint, const Vec3& planeNormal, Vec3& hit);

}  // namespace mi
