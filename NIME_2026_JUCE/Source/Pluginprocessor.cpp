#include "PluginProcessor.h"
#include "PluginEditor.h"

NIMEReceiverProcessor::NIMEReceiverProcessor()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)) {
  startTimer(1000);

  // Automatically attempt to connect to port 8000 on startup
  startOSCReceiver(8000);
}

NIMEReceiverProcessor::~NIMEReceiverProcessor() { stopTimer(); }

// Timer - fires every 1000ms on the message thread
void NIMEReceiverProcessor::timerCallback() {
  oscManager.updateMessagesPerSecond();
}

void NIMEReceiverProcessor::startCalibration() {
  calibState.store((int)CalibState::WaitingPoseA);
}

void NIMEReceiverProcessor::recordPoseA() {
  const auto &d = getIMUData();
  poseAw.store(d.qw.load(std::memory_order_relaxed));
  poseAx.store(d.qx.load(std::memory_order_relaxed));
  poseAy.store(d.qy.load(std::memory_order_relaxed));
  poseAz.store(d.qz.load(std::memory_order_relaxed));
  calibState.store((int)CalibState::WaitingPoseB);
}

void NIMEReceiverProcessor::recordPoseB() {
  const auto &d = getIMUData();
  poseBw.store(d.qw.load(std::memory_order_relaxed));
  poseBx.store(d.qx.load(std::memory_order_relaxed));
  poseBy.store(d.qy.load(std::memory_order_relaxed));
  poseBz.store(d.qz.load(std::memory_order_relaxed));
  calibState.store((int)CalibState::WaitingPoseC);
}

void NIMEReceiverProcessor::recordPoseC() {
  const auto &d = getIMUData();
  poseCw.store(d.qw.load(std::memory_order_relaxed));
  poseCx.store(d.qx.load(std::memory_order_relaxed));
  poseCy.store(d.qy.load(std::memory_order_relaxed));
  poseCz.store(d.qz.load(std::memory_order_relaxed));
  computeCorrection();
  calibState.store((int)CalibState::Done);
}

void NIMEReceiverProcessor::computeCorrection() {
  MathHelpers::Vec3 staffAxis{1.f, 0.f, 0.f};

  MathHelpers::Quat qA{poseAw.load(), poseAx.load(), poseAy.load(),
                       poseAz.load()};
  MathHelpers::Quat qB{poseBw.load(), poseBx.load(), poseBy.load(),
                       poseBz.load()};
  MathHelpers::Quat qC{poseCw.load(), poseCx.load(), poseCy.load(),
                       poseCz.load()};

  // What the sensor actually measured for each pose
  auto b0 = MathHelpers::normalize(MathHelpers::rotate(staffAxis, qA)); // measured "forward"
  auto b1 = MathHelpers::normalize(MathHelpers::rotate(staffAxis, qB)); // measured "up"
  auto b2 = MathHelpers::normalize(MathHelpers::rotate(staffAxis, qC)); // measured "right"

  // What we WANT those directions to map to in virtual space
  MathHelpers::Vec3 r0{1.f, 0.f, 0.f}; // virtual +X (forward)
  MathHelpers::Vec3 r1{0.f, 0.f, 1.f}; // virtual +Z (up)
  MathHelpers::Vec3 r2{0.f, 1.f, 0.f}; // virtual +Y (right)

  // Build the cross-covariance matrix H = sum(r_i * b_i^T)
  // H is 3x3, stored as columns c0, c1, c2
  // H = r0*b0^T + r1*b1^T + r2*b2^T
  // Column j of H = sum_i(r_i * b_i[j])
  float H[3][3] = {};
  auto addOuter = [&](MathHelpers::Vec3 r, MathHelpers::Vec3 b) {
    float rv[3] = {r.x, r.y, r.z};
    float bv[3] = {b.x, b.y, b.z};
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
        H[i][j] += rv[i] * bv[j];
  };
  addOuter(r0, b0);
  addOuter(r1, b1);
  addOuter(r2, b2);

  // For a 3x3 rotation matrix the best-fit solution is R = U * V^T from SVD.
  // Since we have exactly 3 non-degenerate vectors we can extract it directly:
  // Build the best rotation matrix using the explicit polar decomposition shortcut.
  // We compute R such that R * b_i ≈ r_i for all i.
  // The closed-form for 3 vectors: build R directly from the two frames.

  // Build an orthonormal "measured" frame from b0, b1, b2
  auto e0 = b0;
  auto e1 = MathHelpers::normalize(MathHelpers::cross(b0, b1)); // perp to b0 in the b0-b1 plane
  auto e2 = MathHelpers::cross(e0, e1);

  // Build corresponding orthonormal "virtual" frame from r0, r1, r2
  auto f0 = r0;
  auto f1 = MathHelpers::normalize(MathHelpers::cross(r0, r1));
  auto f2 = MathHelpers::cross(f0, f1);

  // The rotation R that takes the measured frame to the virtual frame:
  // R = [f0 f1 f2] * [e0 e1 e2]^T
  // Each f_i is a column of the target, each e_i is a column of the source.
  // R_ij = sum_k f_k[i] * e_k[j]
  float R[3][3];
  float fv[3][3] = {{f0.x, f1.x, f2.x}, {f0.y, f1.y, f2.y}, {f0.z, f1.z, f2.z}};
  float ev[3][3] = {{e0.x, e1.x, e2.x}, {e0.y, e1.y, e2.y}, {e0.z, e1.z, e2.z}};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      R[i][j] = 0.f;
      for (int k = 0; k < 3; ++k)
        R[i][j] += fv[i][k] * ev[j][k]; // f col k row i * e col k row j
    }

  // Convert rotation matrix to quaternion
  // R is stored row-major: R[row][col]
  // Pass columns to fromMatrix
  MathHelpers::Vec3 col0{R[0][0], R[1][0], R[2][0]};
  MathHelpers::Vec3 col1{R[0][1], R[1][1], R[2][1]};
  MathHelpers::Vec3 col2{R[0][2], R[1][2], R[2][2]};
  auto corr = MathHelpers::fromMatrix(col0, col1, col2);

  corrW.store(corr.w);
  corrX.store(corr.x);
  corrY.store(corr.y);
  corrZ.store(corr.z);
}

MathHelpers::Quat NIMEReceiverProcessor::getCalibratedQuat() const {
  const auto &d = getIMUData();
  MathHelpers::Quat q_raw = {d.qw.load(std::memory_order_relaxed),
                             d.qx.load(std::memory_order_relaxed),
                             d.qy.load(std::memory_order_relaxed),
                             d.qz.load(std::memory_order_relaxed)};

  MathHelpers::Quat corr = {corrW.load(std::memory_order_acquire),
                            corrX.load(std::memory_order_acquire),
                            corrY.load(std::memory_order_acquire),
                            corrZ.load(std::memory_order_acquire)};

  // First fix axis alignment, then zero the pose
  return MathHelpers::multiply(corr, q_raw);
}

void NIMEReceiverProcessor::prepareToPlay(double sampleRate,
                                          int samplesPerBlock) {
  synth.prepareToPlay(sampleRate, samplesPerBlock);
}

void NIMEReceiverProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                         juce::MidiBuffer &) {
  // Determine if we have valid live connection data
  bool isReceivingValidData =
      isOSCConnected() && (getMessagesPerSecond() > 0.f);

  // Record orientation into the lock-free circular buffer
  if (isReceivingValidData) {
    auto now = juce::Time::getMillisecondCounter();
    size_t idx = historyWriteIndex.load(std::memory_order_relaxed);
    orientationHistory[idx % orientationHistory.size()] = {getCalibratedQuat(),
                                                           now};
    historyWriteIndex.store(idx + 1, std::memory_order_release);
  }

  // Offload all sound generation and data mapping to the dedicated DSP class
  synth.processBlock(buffer, getCalibratedQuat(), isReceivingValidData);
}

std::vector<OrientationPoint>
NIMEReceiverProcessor::getRecentOrientations(float maxAgeMs) const {
  std::vector<OrientationPoint> recent;
  auto now = juce::Time::getMillisecondCounter();
  size_t writeIdx = historyWriteIndex.load(std::memory_order_acquire);
  size_t startIdx = (writeIdx > orientationHistory.size())
                        ? writeIdx - orientationHistory.size()
                        : 0;

  for (size_t i = startIdx; i < writeIdx; ++i) {
    auto point = orientationHistory[i % orientationHistory.size()];
    if (now - point.timestamp <= maxAgeMs) {
      recent.push_back(point);
    }
  }
  return recent;
}

juce::AudioProcessorEditor *NIMEReceiverProcessor::createEditor() {
  return new NIMEReceiverEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new NIMEReceiverProcessor();
}