// SPDX-License-Identifier: Apache-2.0
// Minimal vector / matrix helpers so the project does not need a math dependency.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace mi {

constexpr float kPi = 3.14159265358979323846f;
inline float radians(float deg) { return deg * (kPi / 180.0f); }

template <class T>
struct V3 {
    T x{}, y{}, z{};

    constexpr V3() = default;
    constexpr V3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
    template <class U>
    constexpr explicit V3(const V3<U>& o) : x(static_cast<T>(o.x)), y(static_cast<T>(o.y)), z(static_cast<T>(o.z)) {}

    constexpr V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr V3 operator*(T s) const { return {x * s, y * s, z * s}; }
    constexpr V3 operator/(T s) const { return {x / s, y / s, z / s}; }
    constexpr V3 operator-() const { return {-x, -y, -z}; }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator-=(const V3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    V3& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }
};

template <class T> constexpr T dot(const V3<T>& a, const V3<T>& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
template <class T> constexpr V3<T> cross(const V3<T>& a, const V3<T>& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
template <class T> inline T length(const V3<T>& a) { return std::sqrt(dot(a, a)); }
template <class T> inline V3<T> normalize(const V3<T>& a) {
    T l = length(a);
    return l > T(1e-20) ? a / l : V3<T>{};
}
template <class T> inline bool isFinite(const V3<T>& a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

using Vec3 = V3<float>;
using Vec3d = V3<double>;

struct AABB {
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    void expand(const Vec3& p) {
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }
    Vec3 center() const { return valid() ? (min + max) * 0.5f : Vec3{}; }
    Vec3 size() const { return valid() ? max - min : Vec3{}; }
    float diagonal() const { return length(size()); }
};

// Column-major 4x4 matrix (OpenGL layout).
struct Mat4 {
    float m[16];

    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.at(row, k) * b.at(k, c);
            r.at(row, c) = s;
        }
    return r;
}

inline Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    return {m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3),
            m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
            m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3)};
}

inline Mat4 perspective(float fovyRad, float aspect, float zNear, float zFar) {
    const float f = 1.0f / std::tan(fovyRad * 0.5f);
    Mat4 r{};
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
    r.at(2, 3) = (2.0f * zFar * zNear) / (zNear - zFar);
    r.at(3, 2) = -1.0f;
    return r;
}

inline Mat4 orthographic(float halfW, float halfH, float zNear, float zFar) {
    Mat4 r = Mat4::identity();
    r.at(0, 0) = 1.0f / halfW;
    r.at(1, 1) = 1.0f / halfH;
    r.at(2, 2) = -2.0f / (zFar - zNear);
    r.at(2, 3) = -(zFar + zNear) / (zFar - zNear);
    return r;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
    r.at(0, 3) = -dot(s, eye);
    r.at(1, 3) = -dot(u, eye);
    r.at(2, 3) = dot(f, eye);
    return r;
}

inline Mat4 rotationX(float rad) {
    Mat4 r = Mat4::identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.at(1, 1) = c; r.at(1, 2) = -s;
    r.at(2, 1) = s; r.at(2, 2) = c;
    return r;
}

}  // namespace mi
