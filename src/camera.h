// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "math3d.h"

namespace mi {

// Turntable-style orbit camera. World +Y is up.
class Camera {
public:
    Vec3 target{0, 0, 0};
    float distance = 5.0f;
    float yawDeg = 35.0f;    // rotation around the up axis, 0 = looking down -Z (front view)
    float pitchDeg = 20.0f;  // elevation above the horizon
    float fovYDeg = 45.0f;
    bool orthographic = false;

    // Size of the thing being looked at; limits zoom and drives the clip planes.
    Vec3 sceneCenter{0, 0, 0};
    float sceneRadius = 1.0f;

    Vec3 forward() const;  // unit vector from the eye towards the target
    Vec3 right() const;
    Vec3 up() const;
    Vec3 position() const { return target - forward() * distance; }

    void orbit(float dxPixels, float dyPixels);
    // `viewportHeight` in the same pixel units as the deltas.
    void pan(float dxPixels, float dyPixels, float viewportHeight);
    // factor < 1 zooms in. (ndcX, ndcY) is the cursor in [-1,1]; the point under it stays put.
    void zoomAt(float factor, float ndcX, float ndcY, float aspect);
    void zoomCentered(float factor);

    void frame(const Vec3& center, float radius);  // fit a sphere into view
    void setView(float yawDeg, float pitchDeg);

    // World-space ray through the point (ndcX, ndcY in [-1,1]) of the view. `dir` is a unit vector.
    void ray(float ndcX, float ndcY, float aspect, Vec3& origin, Vec3& dir) const;
    // World units covered by one screen pixel at `depth` in front of the camera.
    float worldPerPixel(float depth, float viewportHeight) const;

    Mat4 viewMatrix() const;
    Mat4 projectionMatrix(float aspect) const;

private:
    void clampDistance();
    float tanHalfFov() const { return std::tan(radians(fovYDeg) * 0.5f); }
};

}  // namespace mi
