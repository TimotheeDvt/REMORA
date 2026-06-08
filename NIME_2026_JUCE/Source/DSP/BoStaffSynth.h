#pragma once

#include "MathHelpers.h"
#include <JuceHeader.h>
#include <atomic>


class BoStaffSynth {
public:
  BoStaffSynth();
  ~BoStaffSynth();

  void prepareToPlay(double sampleRate, int samplesPerBlock);

  // Main processing block where you can map orientation to sound parameters
  void processBlock(juce::AudioBuffer<float> &buffer,
                    MathHelpers::Quat currentOrientation,
                    bool isReceivingValidData);

  void setSoundEnabled(bool shouldBeEnabled);
  bool isSoundEnabled() const;

private:
  float currentSampleRate = 44100.0f;
  float currentAngle = 0.0f;
  float angleDelta = 0.0f;

  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>
      smoothedFreq{440.0f};
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain{
      0.0f};

  std::atomic<bool> soundEnabled{true};
};