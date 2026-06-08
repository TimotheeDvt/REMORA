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

void NIMEReceiverProcessor::calibrate() {
  const auto &d = getIMUData();
  // Store conjugate of current rotation
  calibW.store(d.qw.load(std::memory_order_relaxed), std::memory_order_relaxed);
  calibX.store(-d.qx.load(std::memory_order_relaxed),
               std::memory_order_relaxed);
  calibY.store(-d.qy.load(std::memory_order_relaxed),
               std::memory_order_relaxed);
  calibZ.store(-d.qz.load(std::memory_order_relaxed),
               std::memory_order_relaxed);
}

MathHelpers::Quat NIMEReceiverProcessor::getCalibratedQuat() const {
  const auto &d = getIMUData();
  const float rw = d.qw.load(std::memory_order_relaxed);
  const float rx = d.qx.load(std::memory_order_relaxed);
  const float ry = d.qy.load(std::memory_order_relaxed);
  const float rz = d.qz.load(std::memory_order_relaxed);

  const float cw = calibW.load(std::memory_order_relaxed);
  const float cx = calibX.load(std::memory_order_relaxed);
  const float cy = calibY.load(std::memory_order_relaxed);
  const float cz = calibZ.load(std::memory_order_relaxed);

  return MathHelpers::Quat{cw * rw - cx * rx - cy * ry - cz * rz,
                           cw * rx + cx * rw + cy * rz - cz * ry,
                           cw * ry - cx * rz + cy * rw + cz * rx,
                           cw * rz + cx * ry - cy * rx + cz * rw};
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