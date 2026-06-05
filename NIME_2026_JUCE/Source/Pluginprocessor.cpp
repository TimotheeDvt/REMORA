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
                                          int /*samplesPerBlock*/) {
  currentSampleRate = static_cast<float>(sampleRate);
  // 5ms smoothing for minimum latency while preventing audio glitches
  smoothedFreq.reset(sampleRate, 0.005);
  smoothedGain.reset(sampleRate, 0.005);
}

void NIMEReceiverProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                         juce::MidiBuffer &) {
  buffer.clear();

  // If not receiving data or sound is muted, smoothly ramp volume to zero
  if (!isOSCConnected() || getMessagesPerSecond() == 0.f || !isSoundEnabled()) {
    smoothedGain.setTargetValue(0.0f);
  } else {
    auto q = getCalibratedQuat();
    auto euler = MathHelpers::toEuler(q);

    // Map Pitch to Frequency (Roughly 100Hz to 1000Hz)
    // Pitch goes from roughly -PI/2 (facing down) to PI/2 (facing up)
    float pitchNorm = (euler.pitch + 1.5708f) / 3.1415f;
    pitchNorm = juce::jlimit(0.0f, 1.0f, pitchNorm);
    float freq = 100.0f + pitchNorm * 900.0f;

    // Map Roll to Volume/Gain (Twist to increase volume)
    float rollNorm = std::abs(euler.roll) / 3.1415f;
    rollNorm = juce::jlimit(0.0f, 1.0f, rollNorm);

    smoothedFreq.setTargetValue(freq);
    smoothedGain.setTargetValue(
        0.05f + rollNorm * 0.15f); // 5% base volume + up to 15% from twist
  }

  if (buffer.getNumChannels() == 0)
    return; // Safely abort if the host didn't provide audio outputs

  auto *left = buffer.getWritePointer(0);
  auto *right =
      buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

  // Generate a continuous sine wave
  for (int i = 0; i < buffer.getNumSamples(); ++i) {
    float cyclesPerSample = smoothedFreq.getNextValue() / currentSampleRate;
    angleDelta = cyclesPerSample * 2.0f * juce::MathConstants<float>::pi;

    float currentSample = std::sin(currentAngle) * smoothedGain.getNextValue();
    currentAngle = std::fmod(currentAngle + angleDelta,
                             2.0f * juce::MathConstants<float>::pi);

    left[i] = currentSample;
    if (right != nullptr)
      right[i] = currentSample;
  }
}

juce::AudioProcessorEditor *NIMEReceiverProcessor::createEditor() {
  return new NIMEReceiverEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new NIMEReceiverProcessor();
}