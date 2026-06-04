#pragma once

#include <cmath>

namespace MathHelpers {
struct Vec3 {
  float x, y, z;
};
struct Quat {
  float w, x, y, z;
};

inline Vec3 rotate(Vec3 v, Quat q) {
  float tx = 2.0f * (q.y * v.z - q.z * v.y);
  float ty = 2.0f * (q.z * v.x - q.x * v.z);
  float tz = 2.0f * (q.x * v.y - q.y * v.x);
  return {v.x + q.w * tx + (q.y * tz - q.z * ty),
          v.y + q.w * ty + (q.z * tx - q.x * tz),
          v.z + q.w * tz + (q.x * ty - q.y * tx)};
}

struct Euler {
  float roll, pitch, yaw;
};

inline Euler toEuler(Quat q) {
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