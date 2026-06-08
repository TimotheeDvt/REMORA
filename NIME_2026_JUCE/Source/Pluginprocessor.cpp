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

MathHelpers::Quat NIMEReceiverProcessor::getMappedRawQuat() const {
  const auto &d = getIMUData();
  float raw[3] = {
      d.qx.load(std::memory_order_relaxed),
      d.qy.load(std::memory_order_relaxed),
      d.qz.load(std::memory_order_relaxed)
  };

  auto getMapped = [&](int mapCode) {
    int axis = mapCode / 2;
    float sign = (mapCode % 2 == 0) ? 1.0f : -1.0f;
    return raw[axis] * sign;
  };

  return {
      d.qw.load(std::memory_order_relaxed),
      getMapped(axisMapX.load(std::memory_order_relaxed)),
      getMapped(axisMapY.load(std::memory_order_relaxed)),
      getMapped(axisMapZ.load(std::memory_order_relaxed))
  };
}

int NIMEReceiverProcessor::getAxisMap(int axisIndex) const {
  if (axisIndex == 0) return axisMapX.load(std::memory_order_relaxed);
  if (axisIndex == 1) return axisMapY.load(std::memory_order_relaxed);
  return axisMapZ.load(std::memory_order_relaxed);
}

void NIMEReceiverProcessor::setAxisMap(int axisIndex, int mapCode) {
  if (axisIndex == 0) axisMapX.store(mapCode, std::memory_order_relaxed);
  else if (axisIndex == 1) axisMapY.store(mapCode, std::memory_order_relaxed);
  else if (axisIndex == 2) axisMapZ.store(mapCode, std::memory_order_relaxed);
}

void NIMEReceiverProcessor::calibrate() {
  auto q = getMappedRawQuat();
  // Store conjugate of current rotation
  calibW.store(q.w, std::memory_order_relaxed);
  calibX.store(-q.x, std::memory_order_relaxed);
  calibY.store(-q.y, std::memory_order_relaxed);
  calibZ.store(-q.z, std::memory_order_relaxed);
}

MathHelpers::Quat NIMEReceiverProcessor::getCalibratedQuat() const {
  auto raw = getMappedRawQuat();
  const float rw = raw.w;
  const float rx = raw.x;
  const float ry = raw.y;
  const float rz = raw.z;

  const float cw = calibW.load(std::memory_order_relaxed);
  const float cx = calibX.load(std::memory_order_relaxed);
  const float cy = calibY.load(std::memory_order_relaxed);
  const float cz = calibZ.load(std::memory_order_relaxed);

  // Multiply Q_raw * Q_calib to map global physical rotation to global virtual rotation
  // (making the visualizer and synth invariant to how the sensor is physically mounted)
  return MathHelpers::Quat{rw * cw - rx * cx - ry * cy - rz * cz,
                           rw * cx + rx * cw + ry * cz - rz * cy,
                           rw * cy - rx * cz + ry * cw + rz * cx,
                           rw * cz + rx * cy - ry * cx + rz * cw};
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
    orientationHistory[idx % orientationHistory.size()] = {getCalibratedQuat(), now};
    historyWriteIndex.store(idx + 1, std::memory_order_release);
  }

  // Offload all sound generation and data mapping to the dedicated DSP class
  synth.processBlock(buffer, getCalibratedQuat(), isReceivingValidData);
}

std::vector<OrientationPoint> NIMEReceiverProcessor::getRecentOrientations(float maxAgeMs) const {
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