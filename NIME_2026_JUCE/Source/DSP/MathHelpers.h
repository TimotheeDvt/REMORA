#pragma once

namespace MathHelpers {
  struct Vec3 { float x, y, z; };
  struct Quat { float w, x, y, z; };

  inline Vec3 rotate(Vec3 v, Quat q)
  {
      float tx = 2.0f * (q.y * v.z - q.z * v.y);
      float ty = 2.0f * (q.z * v.x - q.x * v.z);
      float tz = 2.0f * (q.x * v.y - q.y * v.x);
      return {
          v.x + q.w * tx + (q.y * tz - q.z * ty),
          v.y + q.w * ty + (q.z * tx - q.x * tz),
          v.z + q.w * tz + (q.x * ty - q.y * tx)
      };
  }
}