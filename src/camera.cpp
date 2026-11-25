// SPDX-License-Identifier: Apache-2.0
#include "camera.h"

namespace mi {

Vec3 Camera::forward() const {
    const float y = radians(yawDeg), p = radians(pitchDeg);
    // direction from target to eye, then negated
    return -Vec3{std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y)};
}

Vec3 Camera::right() const { return normalize(cross(forward(), Vec3{0, 1, 0})); }
Vec3 Camera::up() const { return cross(right(), forward()); }

void Camera::orbit(float dx, float dy) {
    constexpr float kDegPerPixel = 0.4f;
    yawDeg -= dx * kDegPerPixel;
    pitchDeg = std::clamp(pitchDeg + dy * kDegPerPixel, -89.9f, 89.9f);
    if (yawDeg > 360.0f) yawDeg -= 360.0f;
    if (yawDeg < -360.0f) yawDeg += 360.0f;
}

void Camera::pan(float dx, float dy, float viewportHeight) {
    if (viewportHeight <= 0) return;
    const float worldPerPixel = 2.0f * distance * tanHalfFov() / viewportHeight;
    target += right() * (-dx * worldPerPixel) + up() * (dy * worldPerPixel);
}

void Camera::clampDistance() {
    distance = std::clamp(distance, sceneRadius * 0.002f, sceneRadius * 200.0f);
}

void Camera::zoomAt(float factor, float ndcX, float ndcY, float aspect) {
    const float before = distance;
    distance *= factor;
    clampDistance();
    const float f = distance / before;  // the factor that was actually applied
    // Point on the focal plane under the cursor; scale its offset from the target so it stays
    // under the cursor after the zoom. Works for perspective and orthographic alike.
    const Vec3 offset = (right() * (ndcX * aspect) + up() * ndcY) * (before * tanHalfFov());
    target += offset * (1.0f - f);
}

void Camera::zoomCentered(float factor) {
    distance *= factor;
    clampDistance();
}

void Camera::frame(const Vec3& center, float radius) {
    sceneCenter = center;
    sceneRadius = std::max(radius, 1e-6f);
    target = center;
    distance = sceneRadius / std::sin(radians(fovYDeg) * 0.5f) * 1.05f;
    clampDistance();
}

void Camera::setView(float yaw, float pitch) {
    yawDeg = yaw;
    pitchDeg = std::clamp(pitch, -89.9f, 89.9f);
}

Mat4 Camera::viewMatrix() const { return lookAt(position(), target, Vec3{0, 1, 0}); }

Mat4 Camera::projectionMatrix(float aspect) const {
    const float offset = length(target - sceneCenter);
    const float zFar = distance + offset + sceneRadius * 6.0f;
    if (orthographic) {
        const float halfH = distance * tanHalfFov();
        const float reach = sceneRadius * 6.0f + offset;
        return mi::orthographic(halfH * aspect, halfH, -reach, zFar);
    }
    const float zNear = std::max(distance * 0.02f, sceneRadius * 1e-4f);
    return perspective(radians(fovYDeg), aspect, zNear, zFar);
}

}  // namespace mi
