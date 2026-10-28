#include "DSP/Calibration.h"
#include "DSP/StaffMotionAnalyzer.h"
#include "DSP/StaffSoundParams.h"
#include "../../firmware/src/GatedMahony.h"
#include <JuceHeader.h>
#include <cstdio>
#include <random>
#include <vector>

// Checks the calibration, orientation filter, and motion analyzer on simulated staff motion, using
// the same code the plugin and firmware run (make analyzer-check). With --replay <log.csv> it
// instead runs a packet log recorded by the plugin and prints thrust detections and facing changes.
//
// Simulation conventions: physical world x forward, y left, z up; staff frame x along the staff
// toward the tip; the sensor is `kRadius` from the centre of rotation (the hands).
namespace {

using MathHelpers::Quat;
using MathHelpers::Vec3;

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = kPi / 180.f;
constexpr float kRadius = 0.7f; // m, sensor to centre of rotation

struct M3 {
  float m[3][3];
  Vec3 operator*(Vec3 v) const {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
  }
  M3 operator*(const M3 &b) const {
    M3 r{};
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 3; ++k) r.m[i][j] += m[i][k] * b.m[k][j];
    return r;
  }
  M3 T() const {
    M3 r{};
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
    return r;
  }
};

M3 diag(float a, float b, float c) { return {{{a, 0, 0}, {0, b, 0}, {0, 0, c}}}; }
const M3 kIdentity = diag(1, 1, 1);
const M3 kChipMirror = diag(-1, 1, 1); // the filter's x remapping (see Calibration.h)

M3 axisAngle(Vec3 a, float angle) {
  a = MathHelpers::normalize(a);
  const float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
  return {{{t * a.x * a.x + c, t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y},
           {t * a.x * a.y + s * a.z, t * a.y * a.y + c, t * a.y * a.z - s * a.x},
           {t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c}}};
}

Quat toQuat(const M3 &r) {
  return MathHelpers::fromMatrix({r.m[0][0], r.m[1][0], r.m[2][0]}, {r.m[0][1], r.m[1][1], r.m[2][1]},
                                 {r.m[0][2], r.m[1][2], r.m[2][2]});
}

float angleBetween(Vec3 a, Vec3 b) {
  return std::acos(juce::jlimit(-1.f, 1.f, MathHelpers::dot(MathHelpers::normalize(a), MathHelpers::normalize(b))));
}

// Quaternion the (converged) filter reports for a chip whose physical orientation is chipToWorld:
// its body frame is the chip frame with x mirrored, its world is physical space mirrored the same
// way, plus an arbitrary yaw.
Quat filterQuat(const M3 &chipToWorld, float yawOffset) {
  return toQuat(axisAngle({0, 0, 1}, yawOffset) * kChipMirror * chipToWorld * kChipMirror);
}

// All 24 proper axis-aligned rotations (each puts some chip axis along the staff).
std::vector<M3> axisAlignedRotations() {
  std::vector<M3> out;
  const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  for (auto &p : perms)
    for (int signs = 0; signs < 8; ++signs) {
      M3 r{};
      for (int i = 0; i < 3; ++i) r.m[i][p[i]] = (signs >> i) & 1 ? -1.f : 1.f;
      const float det = r.m[0][0] * (r.m[1][1] * r.m[2][2] - r.m[1][2] * r.m[2][1]) -
                        r.m[0][1] * (r.m[1][0] * r.m[2][2] - r.m[1][2] * r.m[2][0]) +
                        r.m[0][2] * (r.m[1][0] * r.m[2][1] - r.m[1][1] * r.m[2][0]);
      if (det > 0) out.push_back(r);
    }
  return out;
}

struct Calib {
  Quat align, corr;
  bool ok;
};

Calib calibrateMount(const M3 &mount, float yawOffset, std::mt19937 &rng, float poseErrorDeg) {
  std::uniform_real_distribution<float> roll(-kPi, kPi), err(-poseErrorDeg * kDeg, poseErrorDeg * kDeg);
  auto jitter = [&] { return axisAngle({err(rng), err(rng), err(rng)}, std::sqrt(3.f) * std::abs(err(rng))); };
  const M3 poseA = jitter() * axisAngle({1, 0, 0}, roll(rng));
  const M3 poseB = jitter() * axisAngle({0, 1, 0}, -kPi / 2) * axisAngle({1, 0, 0}, roll(rng));
  const M3 poseC = jitter() * axisAngle({0, 0, 1}, -kPi / 2) * axisAngle({1, 0, 0}, roll(rng));
  const Quat qA = filterQuat(poseA * mount, yawOffset);
  const Quat qB = filterQuat(poseB * mount, yawOffset);
  const Quat qC = filterQuat(poseC * mount, yawOffset);
  Calib c{};
  Quat align;
  c.ok = Calibration::computeAlignQuat(qA, qB, qC, align);
  c.corr = Calibration::computeCorrection(qA, qB, align);
  c.align = MathHelpers::multiply(align, Calibration::computeRollFix(qA, c.corr, align));
  return c;
}

// 1. Calibration over every axis-aligned mount, at eight roll angles about the staff each.
bool checkCalibration() {
  std::printf("\n[1] Calibration: 24 axis-aligned mounts x 8 rolls about the staff, random filter yaw\n");
  std::mt19937 rng(1);
  std::uniform_real_distribution<float> yaw(-kPi, kPi), any(-1, 1);
  const M3 calibratedMirror = diag(1, -1, 1); // calibrated world = physical with y flipped (see Calibration.h)

  int mounts = 0, failures = 0;
  float worstTip = 0, worstRollSpread = 0;
  rng.seed(2);
  for (const M3 &base : axisAlignedRotations())
    for (int r = 0; r < 8; ++r) {
      const M3 mount = axisAngle({1, 0, 0}, r * kPi / 4 + 0.2f) * base;
      const float yawOffset = yaw(rng);
      ++mounts;
      const Calib c = calibrateMount(mount, yawOffset, rng, 0.f);
      if (!c.ok) { ++failures; continue; }
      for (int t = 0; t < 50; ++t) {
        const M3 staff = axisAngle({any(rng), any(rng), any(rng)}, kPi * any(rng));
        const Quat qCal = Calibration::calibrate(filterQuat(staff * mount, yawOffset), c.corr, c.align);
        const Vec3 tip = MathHelpers::rotate({1, 0, 0}, qCal);
        const Vec3 expected = calibratedMirror * (staff * Vec3{1, 0, 0});
        worstTip = std::max(worstTip, angleBetween(tip, expected));
      }
    }
  std::printf("    %d mounts, %d rejected, worst tip error %.3f deg (exact poses)\n", mounts, failures, worstTip / kDeg);

  // Roll zero: the same held staff (same roll in pose A) must read the same roll whatever the mount.
  rng.seed(3);
  for (int t = 0; t < 50; ++t) {
    const M3 staff = axisAngle({any(rng), any(rng), any(rng)}, kPi * any(rng));
    float lo = 1e9f, hi = -1e9f;
    for (const M3 &base : axisAlignedRotations()) {
      const M3 mount = axisAngle({1, 0, 0}, 0.7f) * base;
      const float yawOffset = 1.1f;
      const M3 poseA = axisAngle({1, 0, 0}, 0.f), poseB = axisAngle({0, 1, 0}, -kPi / 2), poseC = axisAngle({0, 0, 1}, -kPi / 2);
      Quat align;
      const Quat qA = filterQuat(poseA * mount, yawOffset), qB = filterQuat(poseB * mount, yawOffset), qC = filterQuat(poseC * mount, yawOffset);
      if (!Calibration::computeAlignQuat(qA, qB, qC, align)) continue;
      const Quat corr = Calibration::computeCorrection(qA, qB, align);
      align = MathHelpers::multiply(align, Calibration::computeRollFix(qA, corr, align));
      const float roll = MathHelpers::toEuler(Calibration::calibrate(filterQuat(staff * mount, yawOffset), corr, align)).roll;
      lo = std::min(lo, roll);
      hi = std::max(hi, roll);
    }
    worstRollSpread = std::max(worstRollSpread, hi - lo);
  }
  std::printf("    roll of the same staff orientation across all 24 mounts: worst spread %.3f deg\n", worstRollSpread / kDeg);

  // Sloppy poses: each pose off by up to 10 deg about a random axis.
  rng.seed(4);
  float worstSloppy = 0;
  int sloppyRejected = 0;
  for (const M3 &base : axisAlignedRotations()) {
    const float yawOffset = yaw(rng);
    const Calib c = calibrateMount(base, yawOffset, rng, 10.f);
    if (!c.ok) { ++sloppyRejected; continue; }
    for (int t = 0; t < 20; ++t) {
      const M3 staff = axisAngle({any(rng), any(rng), any(rng)}, kPi * any(rng));
      const Vec3 tip = MathHelpers::rotate({1, 0, 0}, Calibration::calibrate(filterQuat(staff * base, yawOffset), c.corr, c.align));
      worstSloppy = std::max(worstSloppy, angleBetween(tip, calibratedMirror * (staff * Vec3{1, 0, 0})));
    }
  }
  std::printf("    poses off by up to 10 deg: %d/24 rejected, worst tip error %.1f deg\n", sloppyRejected, worstSloppy / kDeg);

  const bool pass = failures == 0 && worstTip < 0.1f * kDeg && worstRollSpread < 0.1f * kDeg;
  std::printf("    %s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// Physical state of the staff at one instant.
struct StaffState {
  M3 orientation;     // staff frame -> world
  Vec3 omega;         // rad/s, world
  Vec3 linearAccel;   // m/s^2 of the sensor, world (excluding gravity)
};

// Chip-frame readings (g, deg/s) for a staff state, with noise.
void chipReadings(const StaffState &s, const M3 &mount, std::mt19937 &rng, float a[3], float g[3]) {
  std::normal_distribution<float> accelNoise(0.f, 0.01f), gyroNoise(0.f, 0.3f);
  const M3 chipToWorld = s.orientation * mount;
  const Vec3 specificForce = {s.linearAccel.x / 9.81f, s.linearAccel.y / 9.81f, s.linearAccel.z / 9.81f + 1.f};
  const Vec3 fc = chipToWorld.T() * specificForce;
  const Vec3 wc = chipToWorld.T() * s.omega;
  a[0] = fc.x + accelNoise(rng); a[1] = fc.y + accelNoise(rng); a[2] = fc.z + accelNoise(rng);
  g[0] = wc.x / kDeg + gyroNoise(rng); g[1] = wc.y / kDeg + gyroNoise(rng); g[2] = wc.z / kDeg + gyroNoise(rng);
}

// Spin of the staff about `axis` (world) through the hands; the sensor sits kRadius toward the tip.
StaffState spinState(const M3 &start, Vec3 axis, float revPerSec, float t) {
  axis = MathHelpers::normalize(axis);
  const float w = revPerSec * 2 * kPi;
  StaffState s;
  s.orientation = axisAngle(axis, w * t) * start;
  s.omega = {axis.x * w, axis.y * w, axis.z * w};
  const Vec3 r = s.orientation * Vec3{kRadius, 0, 0};
  // Centripetal: -w^2 * (component of r perpendicular to the axis).
  const float along = MathHelpers::dot(r, axis);
  s.linearAccel = {-w * w * (r.x - along * axis.x), -w * w * (r.y - along * axis.y), -w * w * (r.z - along * axis.z)};
  return s;
}

// 2. Tilt error of the orientation filter during spins: fixed-gain Mahony (Kp = 30) vs gated.
bool checkFilterDuringSpins() {
  std::printf("\n[2] Orientation filter tilt error during 4 s spins (200 Hz IMU, sensor %.1f m from the hands)\n", kRadius);
  const M3 mount = axisAngle({1, 0, 0}, 0.6f) * axisAlignedRotations()[5];
  struct Case { const char *name; Vec3 axis; float rev; };
  const Case cases[] = {{"vertical-plane spin, 1 rev/s", {0, 1, 0}, 1.f},
                        {"vertical-plane spin, 2 rev/s", {0, 1, 0}, 2.f},
                        {"overhead spin,       2 rev/s", {0, 0, 1}, 2.f}};
  bool pass = true;
  for (const auto &c : cases) {
    float worst[2] = {0, 0}, settled[2] = {0, 0};
    for (int gated = 0; gated < 2; ++gated) {
      std::mt19937 rng(7);
      GatedMahony f;
      f.gatingEnabled = gated == 1;
      const Quat q0 = filterQuat(mount, 0.f);
      f.setQuaternion(q0.w, q0.x, q0.y, q0.z);
      constexpr float dt = 0.005f;
      for (int i = 0; i < 1600; ++i) {
        const float t = i * dt;
        // Spin for 4 s, then hold the end pose for 4 s.
        const StaffState s = t < 4.f ? spinState(kIdentity, c.axis, c.rev, t)
                                     : StaffState{spinState(kIdentity, c.axis, c.rev, 4.f).orientation, {0, 0, 0}, {0, 0, 0}};
        float a[3], g[3];
        chipReadings(s, mount, rng, a, g);
        f.update(a[0], a[1], a[2], g[0], g[1], g[2], dt);
        // Tilt error: estimated vs true gravity direction in the filter's body frame.
        const Quat q{f.w(), f.x(), f.y(), f.z()};
        const Vec3 estimated = MathHelpers::rotate({0, 0, 1}, MathHelpers::conjugate(q));
        const Vec3 truth = kChipMirror * ((s.orientation * mount).T() * Vec3{0, 0, 1});
        const float err = angleBetween(estimated, truth) / kDeg;
        if (t < 4.f) worst[gated] = std::max(worst[gated], err);
        if (i == 1599) settled[gated] = err;
      }
    }
    std::printf("    %s: worst during spin  fixed-gain %5.1f deg | gated %5.1f deg;  4 s after  %4.1f | %4.1f deg\n",
                c.name, worst[0], worst[1], settled[0], settled[1]);
    pass = pass && worst[1] < 5.f && settled[1] < 1.f;
  }
  std::printf("    %s (gated: < 5 deg during spins, < 1 deg after)\n", pass ? "PASS" : "FAIL");
  return pass;
}

// A simulated packet stream, already calibrated (calibrated world = physical with y flipped).
struct Packet {
  double time;  // sensor time, s
  StaffSoundParams params;
};

Packet makePacket(const StaffState &s, double t, float dt, std::mt19937 &rng) {
  std::normal_distribution<float> accelNoise(0.f, 0.01f), gyroNoise(0.f, 0.3f);
  const M3 flip = diag(1, -1, 1);
  const M3 calibrated = flip * s.orientation * flip; // proper rotation in the calibrated world
  const Quat q = toQuat(calibrated);
  Packet p;
  p.time = t;
  StaffSoundParams &o = p.params;
  o.isReceivingValidData = true;
  o.deltaTimeSeconds = dt;
  o.qw = q.w; o.qx = q.x; o.qy = q.y; o.qz = q.z;
  // Body-frame gyro (any frame works for the analyzer's magnitude and axis-stability features).
  const Vec3 wb = s.orientation.T() * s.omega;
  o.gx = wb.x / kDeg + gyroNoise(rng); o.gy = wb.y / kDeg + gyroNoise(rng); o.gz = wb.z / kDeg + gyroNoise(rng);
  // World-frame vectors, mirrored like the calibrated world. Angular velocity is an axial vector,
  // so under the mirror it picks up an extra sign: w' = -flip * w.
  const Vec3 wFlip = flip * s.omega;
  o.worldGyroX = -wFlip.x / kDeg + gyroNoise(rng); o.worldGyroY = -wFlip.y / kDeg + gyroNoise(rng); o.worldGyroZ = -wFlip.z / kDeg + gyroNoise(rng);
  const Vec3 f = flip * Vec3{s.linearAccel.x / 9.81f, s.linearAccel.y / 9.81f, s.linearAccel.z / 9.81f + 1.f};
  o.worldAccelX = f.x + accelNoise(rng); o.worldAccelY = f.y + accelNoise(rng); o.worldAccelZ = f.z + accelNoise(rng);
  const Vec3 fb = s.orientation.T() * Vec3{s.linearAccel.x / 9.81f, s.linearAccel.y / 9.81f, s.linearAccel.z / 9.81f + 1.f};
  o.ax = fb.x; o.ay = fb.y; o.az = fb.z;
  return p;
}

// What the Spin Classification node does each packet.
struct SpinNode {
  int facingChanges = 0;
  void run(StaffMotionAnalyzer &a, const StaffMotionAnalyzer::DerivedMotionFrame &d) {
    if (d.isMoving) {
      const bool changed = a.updateSpinClassificationByAbsoluteComponent(d.rotationAxisX, d.rotationAxisY, d.rotationAxisZ);
      a.accumulateContinuousSpins(changed, d.spinRateDegPerSec);
    }
    if (a.updateFacingClassification(d.isMoving)) ++facingChanges;
  }
};

// Thrust profile: axial sensor acceleration, accelerate then brake, peak `peakG`, duration 0.25 s.
StaffState thrustState(const M3 &orientation, float peakG, float tSinceStart) {
  StaffState s{orientation, {0, 0, 0}, {0, 0, 0}};
  constexpr float kDuration = 0.25f;
  if (tSinceStart >= 0.f && tSinceStart < kDuration) {
    const float a = peakG * 9.81f * std::sin(2 * kPi * tSinceStart / kDuration);
    const Vec3 tip = orientation * Vec3{1, 0, 0};
    s.linearAccel = {tip.x * a, tip.y * a, tip.z * a};
    s.omega = {0, 0.2f * std::sin(kPi * tSinceStart / kDuration), 0}; // slight wobble, rad/s
  }
  return s;
}

// Runs a stream through the analyzer, either once per packet (the plugin now) or once per audio
// block on the latest packet with the block duration as time step (how it used to be scheduled).
struct RunResult { int thrusts = 0; float onsetPeak = 0; std::vector<double> thrustTimes; };

RunResult runStream(const std::vector<Packet> &packets, int blockSize, bool perPacket) {
  StaffMotionAnalyzer analyzer;
  RunResult r;
  float lastEnvelope = 0;
  auto observe = [&](const StaffMotionAnalyzer::DerivedMotionFrame &d, double t) {
    if (d.axialThrustPeakEnvelope > lastEnvelope + 0.2f) { ++r.thrusts; r.thrustTimes.push_back(t); }
    lastEnvelope = d.axialThrustPeakEnvelope;
    r.onsetPeak = std::max(r.onsetPeak, d.labanTimeSuddenness);
  };
  if (perPacket) {
    for (const auto &p : packets) observe(analyzer.computeFrame(p.params), p.time);
    return r;
  }
  const double blockSeconds = blockSize / 48000.0;
  size_t next = 0;
  const double end = packets.back().time;
  for (double t = 0; t < end; t += blockSeconds) {
    while (next + 1 < packets.size() && packets[next + 1].time <= t) ++next;
    StaffSoundParams p = packets[next].params;
    p.deltaTimeSeconds = static_cast<float>(blockSeconds);
    observe(analyzer.computeFrame(p), t);
  }
  return r;
}

// 3. Thrust detection and buffer-size independence on synthetic thrusts, spins, and swings.
bool checkThrustAndScheduling() {
  std::printf("\n[3] Thrust detector on synthetic motion (100 Hz packets)\n");
  std::mt19937 rng(11);
  std::uniform_real_distribution<float> peak(2.f, 4.f), tilt(-0.5f, 0.5f), jitter(-0.002f, 0.002f);

  // 20 thrusts, 1.5 s apart, staff horizontal-ish at random headings and tilts.
  std::vector<Packet> thrusts;
  std::vector<double> thrustStarts;
  double t = 0;
  for (int k = 0; k < 20; ++k) {
    const M3 orientation = axisAngle({0, 0, 1}, tilt(rng) * kPi) * axisAngle({0, 1, 0}, tilt(rng));
    const float g = peak(rng);
    const double start = t + 0.5;
    thrustStarts.push_back(start);
    for (; t < start + 1.0; t += 0.01)
      thrusts.push_back(makePacket(thrustState(orientation, g, static_cast<float>(t - start)), t + jitter(rng), 0.01f, rng));
  }
  // Negative control: 10 s of vertical-plane spins at 1.5 rev/s, then 10 s of slow tilting swings.
  std::vector<Packet> negatives;
  for (double u = 0; u < 20; u += 0.01) {
    StaffState s = u < 10 ? spinState(kIdentity, {0, 1, 0}, 1.5f, static_cast<float>(u))
                          : StaffState{axisAngle({0, 1, 0}, 0.8f * std::sin(2.f * static_cast<float>(u))), {0, 1.6f * std::cos(2.f * static_cast<float>(u)), 0}, {0, 0, 0}};
    negatives.push_back(makePacket(s, u, 0.01f, rng));
  }

  bool pass = true;
  std::printf("    buffer   per-block (old scheduling)      per-packet (now)\n");
  std::printf("    size     thrusts/20  false  onset pk    thrusts/20  false  onset pk\n");
  RunResult reference;
  for (int block : {64, 128, 256, 512, 1024}) {
    const RunResult oldT = runStream(thrusts, block, false), newT = runStream(thrusts, block, true);
    const RunResult oldN = runStream(negatives, block, false), newN = runStream(negatives, block, true);
    auto hits = [&](const RunResult &r) {
      int n = 0;
      for (double start : thrustStarts)
        for (double tt : r.thrustTimes)
          if (tt >= start && tt < start + 0.3) { ++n; break; }
      return n;
    };
    std::printf("    %5d    %5d       %3d    %.3f       %5d       %3d    %.3f\n", block, hits(oldT),
                oldN.thrusts + oldT.thrusts - hits(oldT), oldT.onsetPeak, hits(newT),
                newN.thrusts + newT.thrusts - hits(newT), newT.onsetPeak);
    if (block == 64) reference = newT;
    pass = pass && hits(newT) == 20 && newN.thrusts == 0 && newT.thrusts == 20 && std::abs(newT.onsetPeak - reference.onsetPeak) < 1e-6f;
  }
  std::printf("    %s (per-packet: 20/20 detected, no false detections, identical at every buffer size)\n", pass ? "PASS" : "FAIL");
  return pass;
}

// 4. Gravity removal: a still, tilted staff must have no dynamic acceleration, whatever the mount.
bool checkGravityRemoval() {
  std::printf("\n[4] Gravity removal on a still staff, 24 mounts x 20 orientations\n");
  std::mt19937 rng(5);
  std::uniform_real_distribution<float> any(-1, 1), yaw(-kPi, kPi);
  float worstNew = 0, worstOld = 0, meanOld = 0;
  int n = 0;
  for (const M3 &base : axisAlignedRotations()) {
    const M3 mount = axisAngle({1, 0, 0}, 0.4f) * base;
    const float yawOffset = yaw(rng);
    const Calib c = calibrateMount(mount, yawOffset, rng, 0.f);
    for (int t = 0; t < 20; ++t) {
      const M3 staff = axisAngle({any(rng), any(rng), any(rng)}, kPi * any(rng));
      IMURawSnapshot p{};
      const Vec3 fc = (staff * mount).T() * Vec3{0, 0, 1};
      p.ax = fc.x; p.ay = fc.y; p.az = fc.z;
      const Quat q = filterQuat(staff * mount, yawOffset);
      p.qw = q.w; p.qx = q.x; p.qy = q.y; p.qz = q.z;
      const StaffSoundParams s = Calibration::toStaffSoundParams(p, c.corr, c.align, 0.01f);
      const float now = std::sqrt(s.worldAccelX * s.worldAccelX + s.worldAccelY * s.worldAccelY + (s.worldAccelZ - 1) * (s.worldAccelZ - 1));
      // Previous formula: raw chip accel minus world +Z rotated into the calibrated body frame.
      const Vec3 gBody = MathHelpers::rotate({0, 0, 1}, MathHelpers::conjugate({s.qw, s.qx, s.qy, s.qz}));
      const float old = std::sqrt((p.ax - gBody.x) * (p.ax - gBody.x) + (p.ay - gBody.y) * (p.ay - gBody.y) + (p.az - gBody.z) * (p.az - gBody.z));
      worstNew = std::max(worstNew, now);
      worstOld = std::max(worstOld, old);
      meanOld += old;
      ++n;
    }
  }
  std::printf("    residual |a_dyn| at rest: now worst %.4f g;  previous formula mean %.2f g, worst %.2f g\n",
              worstNew, meanOld / n, worstOld);
  const bool pass = worstNew < 1e-3f;
  std::printf("    %s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// 5. Facing: overhead spins must not toggle it; a quarter body turn must toggle it once.
bool checkFacing() {
  std::printf("\n[5] Facing\n");
  std::mt19937 rng(9);
  auto run = [&](auto stateAt, double duration, int &oldRuleChanges) {
    StaffMotionAnalyzer a;
    SpinNode node;
    // The previous rule, for comparison: during non-vertical spins, the smoothed tip direction.
    float fx = 1, fy = 0;
    bool north = true;
    oldRuleChanges = 0;
    for (double t = 0; t < duration; t += 0.01) {
      const Packet p = makePacket(stateAt(static_cast<float>(t)), t, 0.01f, rng);
      const auto d = a.computeFrame(p.params);
      node.run(a, d);
      if (t < 0.5) node.facingChanges = 0; // ignore the initial state settling
      if (d.isMoving && !a.isRotationAxisVertical()) {
        fx += 0.2f * (d.tipX - fx);
        fy += 0.2f * (d.tipY - fy);
        const bool was = north;
        north = north ? !(std::abs(fy) > 1.15f * std::abs(fx)) : std::abs(fx) > 1.15f * std::abs(fy);
        if (north != was) ++oldRuleChanges;
      }
    }
    return node.facingChanges;
  };

  int oldOverhead = 0, oldTurn = 0, oldSpinTurn = 0;
  const int overhead = run([](float t) { return spinState(kIdentity, {0, 0, 1}, 2.f, t); }, 5.0, oldOverhead);
  // Still 1 s pointing forward, 90 deg body turn in 0.5 s, still 1 s.
  auto turnState = [](float t) {
    const float u = juce::jlimit(0.f, 1.f, (t - 1.f) / 0.5f);
    const float angle = (kPi / 2) * (0.5f - 0.5f * std::cos(kPi * u));
    const float rate = (t > 1.f && t < 1.5f) ? (kPi / 2) * 0.5f * kPi * std::sin(kPi * u) / 0.5f : 0.f;
    return StaffState{axisAngle({0, 0, 1}, angle), {0, 0, rate}, {0, 0, 0}};
  };
  const int turn = run(turnState, 2.5, oldTurn);
  // Vertical-plane spin in the forward plane, quarter turn, spin in the side plane.
  auto spinTurnSpin = [](float t) {
    if (t < 2.f) return spinState(kIdentity, {0, 1, 0}, 1.5f, t);
    if (t < 2.5f) return StaffState{axisAngle({0, 0, 1}, (kPi / 2) * (t - 2.f) / 0.5f), {0, 0, kPi}, {0, 0, 0}};
    return spinState(axisAngle({0, 0, 1}, kPi / 2), {-1, 0, 0}, 1.5f, t - 2.5f);
  };
  const int spinTurn = run(spinTurnSpin, 4.5, oldSpinTurn);

  std::printf("    overhead spin 2 rev/s, 5 s:         %d changes   (previous rule: %d)\n", overhead, oldOverhead);
  std::printf("    still -> 90 deg turn -> still:      %d change    (expected 1)\n", turn);
  std::printf("    side spin -> 90 deg turn -> spin:   %d change    (expected 1)\n", spinTurn);
  const bool pass = overhead == 0 && turn == 1 && spinTurn == 1;
  std::printf("    %s\n", pass ? "PASS" : "FAIL");
  return pass;
}

// --replay: run a packet log recorded by the plugin through calibration and the analyzer.
int replay(const juce::File &file) {
  juce::StringArray lines;
  file.readLines(lines);
  Quat align{1, 0, 0, 0}, corr{1, 0, 0, 0};
  StaffMotionAnalyzer analyzer;
  SpinNode node;
  IMURawSnapshot previous{};
  bool havePrevious = false;
  int packets = 0, thrusts = 0;
  float lastEnvelope = 0;
  for (const auto &line : lines) {
    if (line.startsWith("#")) {
      auto tokens = juce::StringArray::fromTokens(line.substring(1).trim(), " ", "");
      if (tokens.size() == 5) {
        const Quat q{tokens[1].getFloatValue(), tokens[2].getFloatValue(), tokens[3].getFloatValue(), tokens[4].getFloatValue()};
        if (tokens[0] == "align") align = q;
        if (tokens[0] == "corr") corr = q;
      }
      continue;
    }
    auto f = juce::StringArray::fromTokens(line, ",", "");
    if (f.size() < 16 || f[0] == "host_s") continue;
    IMURawSnapshot p{};
    p.sequence = static_cast<uint32_t>(f[1].getLargeIntValue());
    p.sensorMicros = static_cast<uint32_t>(f[2].getLargeIntValue());
    p.hasSensorClock = p.sensorMicros != 0;
    float *dst[13] = {&p.ax, &p.ay, &p.az, &p.gx, &p.gy, &p.gz, &p.mx, &p.my, &p.mz, &p.qw, &p.qx, &p.qy, &p.qz};
    for (int i = 0; i < 13; ++i) *dst[i] = f[3 + i].getFloatValue();
    const double hostSeconds = f[0].getDoubleValue();
    float dt = 0.01f;
    if (havePrevious && p.hasSensorClock) {
      const double e = static_cast<uint32_t>(p.sensorMicros - previous.sensorMicros) * 1e-6;
      if (e > 0.0005 && e < 0.1) dt = static_cast<float>(e);
    }
    previous = p;
    havePrevious = true;
    const auto d = analyzer.computeFrame(Calibration::toStaffSoundParams(p, corr, align, dt));
    const bool wasNorth = analyzer.isFacingNorth();
    node.run(analyzer, d);
    if (analyzer.isFacingNorth() != wasNorth)
      std::printf("%10.3f s  facing -> %s\n", hostSeconds, analyzer.isFacingNorth() ? "across" : "along");
    if (d.axialThrustPeakEnvelope > lastEnvelope + 0.2f) {
      ++thrusts;
      std::printf("%10.3f s  THRUST (envelope %.2f)\n", hostSeconds, d.axialThrustPeakEnvelope);
    }
    lastEnvelope = d.axialThrustPeakEnvelope;
    ++packets;
  }
  std::printf("%d packets, %d thrusts, %d facing changes\n", packets, thrusts, node.facingChanges);
  return 0;
}

} // namespace

int main(int argc, char *argv[]) {
  juce::ScopedJuceInitialiser_GUI init;
  if (argc >= 3 && juce::String(argv[1]) == "--replay")
    return replay(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));

  bool ok = true;
  ok &= checkCalibration();
  ok &= checkFilterDuringSpins();
  ok &= checkThrustAndScheduling();
  ok &= checkGravityRemoval();
  ok &= checkFacing();
  std::printf("\n%s\n", ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
  return ok ? 0 : 1;
}
