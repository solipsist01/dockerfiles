#pragma once
// Eigen wiskundebibliotheek: vectoren en 4x4 matrices (column-major, zoals OpenGL verwacht).
#include <cmath>
#include <cstdint>

constexpr float PI = 3.14159265358979f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) { float l = length(a); return l > 0 ? a * (1.0f / l) : a; }
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float wrapAngle(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

struct Mat4 {
    float m[16];
    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
        return r;
    }
    static Mat4 perspective(float fovy, float aspect, float n, float f) {
        Mat4 r{};
        float t = 1.0f / std::tan(fovy / 2);
        r.m[0] = t / aspect;
        r.m[5] = t;
        r.m[10] = (f + n) / (n - f);
        r.m[11] = -1;
        r.m[14] = (2 * f * n) / (n - f);
        return r;
    }
    static Mat4 translation(float x, float y, float z) {
        Mat4 r = identity();
        r.m[12] = x; r.m[13] = y; r.m[14] = z;
        return r;
    }
    // Verplaatsing + draaiing om de y-as. Lokale +z (voorkant) wijst naar (sin h, 0, cos h).
    // Bij heading h is lokale -x de rechterkant van het voertuig.
    static Mat4 pose(float x, float y, float z, float h) {
        Mat4 r{};
        float c = std::cos(h), s = std::sin(h);
        r.m[0] = c; r.m[2] = -s;
        r.m[5] = 1;
        r.m[8] = s; r.m[10] = c;
        r.m[12] = x; r.m[13] = y; r.m[14] = z; r.m[15] = 1;
        return r;
    }
    static Mat4 rotX(float a) {
        Mat4 r = identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }
    static Mat4 rotY(float a) {
        Mat4 r = identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }
    static Mat4 rotZ(float a) {
        Mat4 r = identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }
    static Mat4 scale(float x, float y, float z) {
        Mat4 r = identity();
        r.m[0] = x; r.m[5] = y; r.m[10] = z;
        return r;
    }
    Vec3 point(Vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    Vec3 dir(Vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z, m[2] * p.x + m[6] * p.y + m[10] * p.z};
    }
    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
        Vec3 f = normalize(target - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r{};
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] = dot(f, eye);
        r.m[15] = 1;
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
