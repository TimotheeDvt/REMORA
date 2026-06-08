#include "BoStaffSynth.h"

BoStaffSynth::BoStaffSynth() {}

BoStaffSynth::~BoStaffSynth() {}

void BoStaffSynth::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
  currentSampleRate = static_cast<float>(sampleRate);
  // 5ms smoothing for minimum latency while preventing audio glitches
  smoothedFreq.reset(sampleRate, 0.005);
  smoothedGain.reset(sampleRate, 0.005);
}

void BoStaffSynth::setSoundEnabled(bool shouldBeEnabled) {
  soundEnabled.store(shouldBeEnabled);
}

bool BoStaffSynth::isSoundEnabled() const { return soundEnabled.load(); }

void BoStaffSynth::processBlock(juce::AudioBuffer<float> &buffer,
                                MathHelpers::Quat orientation,
                                bool isReceivingValidData) {
  buffer.clear();

  // If not receiving data or sound is muted, smoothly ramp volume to zero
  if (!isReceivingValidData || !isSoundEnabled()) {
    smoothedGain.setTargetValue(0.0f);
  } else {
    auto euler = MathHelpers::toEuler(orientation);

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
    return;

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