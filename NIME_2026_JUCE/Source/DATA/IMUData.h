#pragma once

#include <atomic>

struct IMURawSnapshot {
  float ax, ay, az;
  float gx, gy, gz;
  float mx, my, mz;
  float qw, qx, qy, qz;
};

struct IMUData {
  // Accelerometer (g)
  std::atomic<float> ax{0.f}, ay{0.f}, az{0.f};
  // Gyroscope (deg/s)
  std::atomic<float> gx{0.f}, gy{0.f}, gz{0.f};
  // Magnetometer (µT)
  std::atomic<float> mx{0.f}, my{0.f}, mz{0.f};
  // Orientation Quaternion
  std::atomic<float> qw{1.f}, qx{0.f}, qy{0.f}, qz{0.f};

  // Seqlock for consistent snapshot reads
  std::atomic<unsigned> seq{0};

  // Call from writer after updating all fields
  void commitSeq() {
    seq.fetch_add(1, std::memory_order_release);
  }

  // Returns false if a write raced; caller should retry
  bool trySnapshot(IMURawSnapshot& out) const {
    unsigned s1 = seq.load(std::memory_order_acquire);
    if (s1 & 1) return false; // write in progress
    out.ax = ax.load(std::memory_order_relaxed);
    out.ay = ay.load(std::memory_order_relaxed);
    out.az = az.load(std::memory_order_relaxed);
    out.gx = gx.load(std::memory_order_relaxed);
    out.gy = gy.load(std::memory_order_relaxed);
    out.gz = gz.load(std::memory_order_relaxed);
    out.mx = mx.load(std::memory_order_relaxed);
    out.my = my.load(std::memory_order_relaxed);
    out.mz = mz.load(std::memory_order_relaxed);
    out.qw = qw.load(std::memory_order_relaxed);
    out.qx = qx.load(std::memory_order_relaxed);
    out.qy = qy.load(std::memory_order_relaxed);
    out.qz = qz.load(std::memory_order_relaxed);
    unsigned s2 = seq.load(std::memory_order_acquire);
    return s1 == s2;
  }
};