#include "PluginProcessor.h"
#include "PluginEditor.h"

NIMEReceiverProcessor::NIMEReceiverProcessor()
    : AudioProcessor(BusesProperties()) {
  startTimer(1000);

  // Automatically attempt to connect to port 8000 on startup
  startOSCReceiver(8000);
}

NIMEReceiverProcessor::~NIMEReceiverProcessor() {
  stopTimer();
}

// Timer - fires every 1000ms on the message thread
void NIMEReceiverProcessor::timerCallback() {
  oscManager.updateMessagesPerSecond();
}

void NIMEReceiverProcessor::calibrate() {
  const auto &d = getIMUData();
  // Store conjugate of current rotation
  calibW.store(d.qw.load(std::memory_order_relaxed), std::memory_order_relaxed);
  calibX.store(-d.qx.load(std::memory_order_relaxed), std::memory_order_relaxed);
  calibY.store(-d.qy.load(std::memory_order_relaxed), std::memory_order_relaxed);
  calibZ.store(-d.qz.load(std::memory_order_relaxed), std::memory_order_relaxed);
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

  return MathHelpers::Quat{
      cw * rw - cx * rx - cy * ry - cz * rz,
      cw * rx + cx * rw + cy * rz - cz * ry,
      cw * ry - cx * rz + cy * rw + cz * rx,
      cw * rz + cx * ry - cy * rx + cz * rw};
}

juce::AudioProcessorEditor *NIMEReceiverProcessor::createEditor() {
  return new NIMEReceiverEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new NIMEReceiverProcessor();
}