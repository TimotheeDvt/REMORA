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
  computeCorrection();
  calibState.store((int)CalibState::Done);
}

void NIMEReceiverProcessor::computeCorrection() {
  // The staff tip direction in sensor space for each pose
  // (staff long axis is virtual X = {1,0,0}, rotated by the recorded quat)
  MathHelpers::Vec3 staffAxis{1.f, 0.f, 0.f};

  MathHelpers::Quat qA{poseAw.load(), poseAx.load(), poseAy.load(),
                       poseAz.load()};
  MathHelpers::Quat qB{poseBw.load(), poseBx.load(), poseBy.load(),
                       poseBz.load()};

  // Physical directions the sensor measured for each pose
  auto physA =
      MathHelpers::rotate(staffAxis, qA); // should become virtual (1,0,0)
  auto physB =
      MathHelpers::rotate(staffAxis, qB); // should become virtual (0,0,1)

  auto cross = [](MathHelpers::Vec3 a, MathHelpers::Vec3 b) {
    return MathHelpers::Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                             a.x * b.y - a.y * b.x};
  };
  auto norm = [](MathHelpers::Vec3 v) {
    float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 0.0f ? MathHelpers::Vec3{v.x / l, v.y / l, v.z / l}
                    : MathHelpers::Vec3{1.f, 0.f, 0.f};
  };

  // Triad method: build two orthonormal frames, find rotation between them
  auto r1 = norm(physA);
  auto r2 = norm(cross(physA, physB));
  auto r3 = cross(r1, r2);

  // Virtual target frame: X=(1,0,0), Y=(0,1,0), Z=(0,0,1)
  // Rotation matrix from physical to virtual = R_virtual * R_physical^T
  // Convert that 3x3 matrix to a quaternion:
  float m00 = r1.x, m01 = r2.x, m02 = r3.x;
  float m10 = r1.y, m11 = r2.y, m12 = r3.y;
  float m20 = r1.z, m21 = r2.z, m22 = r3.z;

  float trace = m00 + m11 + m22;
  float cw, cx, cy, cz;
  if (trace > 0.f) {
    float s = 0.5f / std::sqrt(trace + 1.f);
    cw = 0.25f / s;
    cx = (m21 - m12) * s;
    cy = (m02 - m20) * s;
    cz = (m10 - m01) * s;
  } else if (m00 > m11 && m00 > m22) {
    float s = 2.f * std::sqrt(1.f + m00 - m11 - m22);
    cw = (m21 - m12) / s;
    cx = 0.25f * s;
    cy = (m01 + m10) / s;
    cz = (m02 + m20) / s;
  } else if (m11 > m22) {
    float s = 2.f * std::sqrt(1.f + m11 - m00 - m22);
    cw = (m02 - m20) / s;
    cx = (m01 + m10) / s;
    cy = 0.25f * s;
    cz = (m12 + m21) / s;
  } else {
    float s = 2.f * std::sqrt(1.f + m22 - m00 - m11);
    cw = (m10 - m01) / s;
    cx = (m02 + m20) / s;
    cy = (m12 + m21) / s;
    cz = 0.25f * s;
  }

  corrW.store(cw);
  corrX.store(cx);
  corrY.store(cy);
  corrZ.store(cz);
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