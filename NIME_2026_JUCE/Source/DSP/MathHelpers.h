#pragma once

#include <cmath>

namespace MathHelpers {
struct Vec3 {
  float x, y, z;
};
struct Quat {
  float w, x, y, z;
};

[[nodiscard]] constexpr inline Vec3 rotate(Vec3 v, Quat q) {
  float tx = 2.0f * (q.y * v.z - q.z * v.y);
  float ty = 2.0f * (q.z * v.x - q.x * v.z);
  float tz = 2.0f * (q.x * v.y - q.y * v.x);
  return {v.x + q.w * tx + (q.y * tz - q.z * ty),
          v.y + q.w * ty + (q.z * tx - q.x * tz),
          v.z + q.w * tz + (q.x * ty - q.y * tx)};
}

[[nodiscard]] constexpr inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] constexpr inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] inline float norm(Vec3 v) { return std::sqrt(dot(v, v)); }
[[nodiscard]] inline Vec3 normalize(Vec3 v) {
  float l = norm(v);
  return l > 0.0f ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{1.0f, 0.0f, 0.0f};
}

[[nodiscard]] inline Quat fromTwoVectors(Vec3 from, Vec3 to) {
  from = normalize(from);
  to = normalize(to);
  Vec3 axis = cross(from, to);
  float d = dot(from, to);
  float s = std::sqrt((1.f + d) * 2.f);
  if (s < 1e-6f)
    return {0.f, 1.f, 0.f, 0.f}; // 180 degree edge case
  return {s * 0.5f, axis.x / s, axis.y / s, axis.z / s};
}

[[nodiscard]] constexpr inline Vec3 rotateByQuat(Vec3 v, Quat q) { return rotate(v, q); }

[[nodiscard]] constexpr inline Quat multiply(Quat q1, Quat q2) {
  return {q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z,
          q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
          q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x,
          q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w};
}
[[nodiscard]] constexpr inline Quat conjugate(Quat q) { return {q.w, -q.x, -q.y, -q.z}; }
[[nodiscard]] constexpr inline Quat inverse(Quat q) {
  return conjugate(q);
} // Assuming unit quaternions

[[nodiscard]] inline Quat fromMatrix(Vec3 c0, Vec3 c1, Vec3 c2) {
  float trace = c0.x + c1.y + c2.z;
  if (trace > 0.0f) {
    float s = 0.5f / std::sqrt(trace + 1.0f);
    return {0.25f / s, (c1.z - c2.y) * s, (c2.x - c0.z) * s, (c0.y - c1.x) * s};
  }
  if (c0.x > c1.y && c0.x > c2.z) {
    float s = 2.0f * std::sqrt(1.0f + c0.x - c1.y - c2.z);
    return {(c1.z - c2.y) / s, 0.25f * s, (c0.y + c1.x) / s, (c2.x + c0.z) / s};
  } else if (c1.y > c2.z) {
    float s = 2.0f * std::sqrt(1.0f + c1.y - c0.x - c2.z);
    return {(c2.x - c0.z) / s, (c0.y + c1.x) / s, 0.25f * s, (c1.z + c2.y) / s};
  }
  float s = 2.0f * std::sqrt(1.0f + c2.z - c0.x - c1.y);
  return {(c0.y - c1.x) / s, (c2.x + c0.z) / s, (c1.z + c2.y) / s, 0.25f * s};
}

struct Euler {
  float roll, pitch, yaw;
};

[[nodiscard]] inline Euler toEuler(Quat q) {
  Euler e;
  // Roll (X-axis rotation)
  float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
  float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
  e.roll = std::atan2(sinr_cosp, cosr_cosp);

  // Pitch (Y-axis rotation)
  float sinp = 2.0f * (q.w * q.y - q.z * q.x);
  if (std::abs(sinp) >= 1.0f)
    e.pitch = std::copysign(3.14159265358979323846f / 2.0f,
                            sinp); // use 90 degrees if out of range
  else
    e.pitch = std::asin(sinp);

  // Yaw (Z-axis rotation)
  float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
  float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
  e.yaw = std::atan2(siny_cosp, cosy_cosp);
  return e;
}
} // namespace MathHelpers