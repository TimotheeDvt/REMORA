#pragma once

#include <atomic>

struct IMUData {
  // Accelerometer (g)
  std::atomic<float> ax{0.f}, ay{0.f}, az{0.f};
  // Gyroscope (deg/s)
  std::atomic<float> gx{0.f}, gy{0.f}, gz{0.f};
  // Magnetometer (µT)
  std::atomic<float> mx{0.f}, my{0.f}, mz{0.f};
  // Orientation Quaternion
  std::atomic<float> qw{1.f}, qx{0.f}, qy{0.f}, qz{0.f};
};