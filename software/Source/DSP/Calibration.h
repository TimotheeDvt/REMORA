#pragma once

#include "MathHelpers.h"
#include "StaffSoundParams.h"
#include "../DATA/IMUData.h"
#include <cmath>

// Calibration and sensor-to-instrument conversion, kept free of plugin state so the check tool
// (Tools/AnalyzerCheck.cpp) runs exactly the code the plugin runs.
//
// Frame convention. The firmware's orientation filter (like the hideakitai/MPU9250 filter it
// replaces) is fed accel (-ax, ay, az) and gyro (gx, -gy, -gz): the chip's x axis is mirrored.
// The quaternion therefore rotates this mirrored "filter body" frame into a world frame with z up
// that is itself a mirror image of physical space. Every vector the plugin uses is expressed in
// that frame, so all of it is self-consistent, but cross products computed in it have the opposite
// sign to the physical ones. This is why calibration picks the staff axis with a negative triple
// product (see computeAlignQuat).
namespace Calibration {

using MathHelpers::Quat;
using MathHelpers::Vec3;

inline Vec3 accelToFilterBody(float ax, float ay, float az) { return {-ax, ay, az}; }
inline Vec3 gyroToFilterBody(float gx, float gy, float gz) { return {gx, -gy, -gz}; }

// Picks which of the six filter-body axes runs along the staff, from three poses:
// A horizontal pointing forward, B vertical pointing up, C horizontal pointing right.
// The staff axis is the only one carried through three mutually orthogonal directions, so it has
// the lowest pairwise |dot|. Of L and -L, the tip is the one that points forward, up and right in
// A, B, C. Physically forward x up = right, so its triple product would be +1; in the mirrored
// filter frame (see above) it is -1, hence the `< 0` test.
inline bool computeAlignQuat(Quat qA, Quat qB, Quat qC, Quat &qOut) {
  const Vec3 candidates[6] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                              {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}};

  int bestIndex = -1;
  float minError = 1e9f;
  for (int i = 0; i < 6; ++i) {
    const Vec3 &L = candidates[i];
    const Vec3 vA = MathHelpers::normalize(MathHelpers::rotate(L, qA));
    const Vec3 vB = MathHelpers::normalize(MathHelpers::rotate(L, qB));
    const Vec3 vC = MathHelpers::normalize(MathHelpers::rotate(L, qC));
    const float err = std::abs(MathHelpers::dot(vA, vB)) + std::abs(MathHelpers::dot(vB, vC)) +
                      std::abs(MathHelpers::dot(vC, vA));
    const float handedness = MathHelpers::dot(MathHelpers::cross(vA, vB), vC);
    if (handedness < -0.5f && err < minError) {
      minError = err;
      bestIndex = i;
    }
  }

  // Poses too close together or coplanar: no axis is clearly orthogonal across all three
  // (1.5 allows each pair of poses to be ~30deg off a right angle).
  constexpr float kMaxOrthogonalityError = 1.5f;
  if (bestIndex < 0 || minError > kMaxOrthogonalityError) {
    qOut = {1.f, 0.f, 0.f, 0.f};
    return false;
  }

  // Each case is the quaternion rotating canonical +X onto that axis; 0.70710678f = sin(45deg) = cos(45deg): w and axis component of a 90deg rotation.
  switch (bestIndex) {
    case 0: qOut = {1.f, 0.f, 0.f, 0.f}; break;                  // +X
    case 1: qOut = {0.f, 0.f, 1.f, 0.f}; break;                  // -X
    case 2: qOut = {0.70710678f, 0.f, 0.f, 0.70710678f}; break;  // +Y
    case 3: qOut = {0.70710678f, 0.f, 0.f, -0.70710678f}; break; // -Y
    case 4: qOut = {0.70710678f, 0.f, -0.70710678f, 0.f}; break; // +Z
    case 5: qOut = {0.70710678f, 0.f, 0.70710678f, 0.f}; break;  // -Z
    default: qOut = {1.f, 0.f, 0.f, 0.f}; break;
  }
  return true;
}

// Rotation taking the staff direction measured in poses A (b0) and B (b1) onto instrument forward
// (1,0,0) and up (0,0,1): R = W U^T with U = [b0, n, b0 x n], n = unit(b0 x b1), and W likewise.
inline Quat computeCorrection(Quat qA, Quat qB, Quat qAlign) {
  const Vec3 staffAxis = MathHelpers::rotate({1.f, 0.f, 0.f}, qAlign);
  const Vec3 b0 = MathHelpers::normalize(MathHelpers::rotate(staffAxis, qA));
  const Vec3 b1 = MathHelpers::normalize(MathHelpers::rotate(staffAxis, qB));
  const Vec3 r0{1.f, 0.f, 0.f};
  const Vec3 r1{0.f, 0.f, 1.f};

  const Vec3 e0 = b0;
  const Vec3 e1 = MathHelpers::normalize(MathHelpers::cross(b0, b1));
  const Vec3 e2 = MathHelpers::cross(e0, e1);
  const Vec3 f0 = r0;
  const Vec3 f1 = MathHelpers::normalize(MathHelpers::cross(r0, r1));
  const Vec3 f2 = MathHelpers::cross(f0, f1);

  float R[3][3];
  const float fv[3][3] = {{f0.x, f1.x, f2.x}, {f0.y, f1.y, f2.y}, {f0.z, f1.z, f2.z}};
  const float ev[3][3] = {{e0.x, e1.x, e2.x}, {e0.y, e1.y, e2.y}, {e0.z, e1.z, e2.z}};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      R[i][j] = 0.f;
      for (int k = 0; k < 3; ++k)
        R[i][j] += fv[i][k] * ev[j][k];
    }

  // R is row-major; fromMatrix expects columns.
  return MathHelpers::fromMatrix({R[0][0], R[1][0], R[2][0]}, {R[0][1], R[1][1], R[2][1]},
                                 {R[0][2], R[1][2], R[2][2]});
}

// Roll about the staff that makes pose A read as zero roll. Without it, roll would be measured from
// however the enclosure happens to be rotated around the staff; with it, from how the performer held
// the staff in pose A. Applied on the body side, so it leaves the staff axis (and the tip) unchanged.
inline Quat computeRollFix(Quat qA, Quat qCorr, Quat qAlign) {
  const Quat poseA = MathHelpers::multiply(qCorr, MathHelpers::multiply(qA, qAlign));
  const Vec3 y = MathHelpers::rotate({0.f, 1.f, 0.f}, poseA);
  const float halfRoll = -0.5f * std::atan2(y.z, y.y);
  return {std::cos(halfRoll), std::sin(halfRoll), 0.f, 0.f};
}

// Instrument orientation: align the staff axis to X, apply the sensor's orientation, then the
// heading/twist correction.
inline Quat calibrate(Quat qRaw, Quat qCorr, Quat qAlign) {
  return MathHelpers::multiply(qCorr, MathHelpers::multiply(qRaw, qAlign));
}

// Correction that turns the current staff heading into "forward", leaving tilt untouched. Fails
// (returns false) when the staff is within ~37deg of vertical, where its heading is ill-defined.
inline bool rezeroHeading(Quat qCalibrated, Quat qCorr, Quat &qCorrOut) {
  const Vec3 tip = MathHelpers::rotate({1.f, 0.f, 0.f}, qCalibrated);
  const float horizontal = std::sqrt(tip.x * tip.x + tip.y * tip.y);
  if (horizontal < 0.6f)
    return false;
  const float halfYaw = -0.5f * std::atan2(tip.y, tip.x);
  const Quat undoYaw{std::cos(halfYaw), 0.f, 0.f, std::sin(halfYaw)};
  qCorrOut = MathHelpers::multiply(undoYaw, qCorr);
  return true;
}

// Converts one packet to what the mappings see. Gyro and accel are rotated into the same world
// frame as the calibrated quaternion, so the analyzer can subtract gravity and read the spin axis
// without mixing frames.
inline StaffSoundParams toStaffSoundParams(const IMURawSnapshot &p, Quat qCorr, Quat qAlign,
                                           float deltaTimeSeconds) {
  StaffSoundParams params;
  params.isReceivingValidData = true;
  params.deltaTimeSeconds = deltaTimeSeconds;

  const Quat qRaw{p.qw, p.qx, p.qy, p.qz};
  const Quat qCal = calibrate(qRaw, qCorr, qAlign);
  const auto euler = MathHelpers::toEuler(qCal);
  params.pitch = euler.pitch;
  params.roll = euler.roll;
  params.yaw = euler.yaw;
  params.qw = qCal.w;
  params.qx = qCal.x;
  params.qy = qCal.y;
  params.qz = qCal.z;

  params.ax = p.ax; params.ay = p.ay; params.az = p.az;
  params.gx = p.gx; params.gy = p.gy; params.gz = p.gz;
  params.mx = p.mx; params.my = p.my; params.mz = p.mz;

  // Filter-body vectors map to the world by qCorr * qRaw (qAlign only relabels the body axes).
  const Quat bodyToWorld = MathHelpers::multiply(qCorr, qRaw);
  const Vec3 w = MathHelpers::rotate(gyroToFilterBody(p.gx, p.gy, p.gz), bodyToWorld);
  const Vec3 a = MathHelpers::rotate(accelToFilterBody(p.ax, p.ay, p.az), bodyToWorld);
  params.worldGyroX = w.x; params.worldGyroY = w.y; params.worldGyroZ = w.z;
  params.worldAccelX = a.x; params.worldAccelY = a.y; params.worldAccelZ = a.z;
  return params;
}

} // namespace Calibration
