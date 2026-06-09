#pragma once

#include "MathHelpers.h"
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>

#include "IMappingStrategy.h"
#include "Mappings/BowedChordMapping.h"
#include "Mappings/SimpleMapping.h"
#include "Mappings/LeadDroneMapping.h"
#include "Mappings/SpinFilterMapping.h"
#include <memory>

struct StaffSoundParams {
    float pitch = 0.f;
    float roll  = 0.f;
    float yaw   = 0.f;
    float gx = 0.f, gy = 0.f, gz = 0.f;
    float ax = 0.f, ay = 0.f, az = 0.f;
    bool  isReceivingValidData = false;
};

struct SynthVoice {
    static constexpr int kPartials = 6;

    float phase[kPartials]   = {};
    float detune = 0.0f;
    float targetAmp = 0.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> partialAmp[kPartials];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> voiceAmp;

    static constexpr int kCombSize = 2048;
    float combBuffer[kCombSize] = {};
    int   combWrite = 0;

    void prepare(double sampleRate) {
        voiceAmp.reset(sampleRate, 0.040);
        voiceAmp.setCurrentAndTargetValue(0.0f);
        constexpr float kPartialSmoothMs[kPartials] = { 15, 20, 25, 30, 40, 50 };
        for (int p = 0; p < kPartials; ++p) {
            partialAmp[p].reset(sampleRate, kPartialSmoothMs[p] * 0.001);
            partialAmp[p].setCurrentAndTargetValue(0.0f);
        }
    }

    float tick(float fundamentalHz, float sampleRateRecip,
               const float partialTargets[kPartials],
               float noiseAmt, uint32_t& rng, float driveAmt)
    {
        constexpr float twoPi = 6.28318530717958f;
        for (int p = 0; p < kPartials; ++p)
            partialAmp[p].setTargetValue(partialTargets[p]);

        float out = 0.f;
        for (int p = 0; p < kPartials; ++p) {
            float harmHz = fundamentalHz * float(p + 1);
            float delta  = harmHz * sampleRateRecip * twoPi;
            phase[p] += delta;
            if (phase[p] >= twoPi) phase[p] -= twoPi;
            out += std::sin(phase[p]) * partialAmp[p].getNextValue();
        }

        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        float noise = static_cast<float>(rng) * (2.0f / 4294967296.0f) - 1.0f;
        out += noise * noiseAmt;

        if (driveAmt > 0.0f) {
            float d = 1.0f + driveAmt;
            out = (out * d) / (1.0f + std::abs(out * d));
        }

        int readIdx = (combWrite + kCombSize - 1800) % kCombSize;
        float comb  = combBuffer[readIdx];
        combBuffer[combWrite] = out + comb * 0.35f;
        combWrite = (combWrite + 1) % kCombSize;
        out = out * 0.7f + comb * 0.3f;

        return out;
    }
};

struct ChordType {
    const char* name;
    float semitones[4];
    int   numVoices;
};

class BoStaffSynth {
public:
  BoStaffSynth();
  ~BoStaffSynth();

  void prepareToPlay(double sampleRate, int samplesPerBlock);

  void processBlock(juce::AudioBuffer<float> &buffer,
                    const StaffSoundParams& params);

  void setSoundEnabled(bool shouldBeEnabled);
  bool isSoundEnabled() const;

  void setMappingStrategy(int index);
  int  getMappingStrategy() const noexcept;

  static const char* getMappingName(int index);
  static int         getMappingCount();

private:
  static constexpr int kNumVoices  = 4;

  float currentSampleRate  = 44100.f;
  float sampleRateRecip    = 1.f / 44100.f;

  SynthVoice voices[kNumVoices];

  SimpleMapping     mappingSimple;
  BowedChordMapping mappingBowedChord;
  LeadDroneMapping  mappingLeadDrone;
  SpinFilterMapping mappingSpinFilter;

  std::atomic<int> activeMappingIndex{1};

  MappingOutput mappingOut;

  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain{0.f};
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bowPressure{0.f};
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rootFreq{110.f};

  float vibratoPhase  = 0.f;
  float tremoloPhase  = 0.f;
  float chorusPhase[kNumVoices] = {};

  float noiseFilterState = 0.f;

  float prevChordSemitones[3] = { 7.f, 12.f, 19.f };
  float blendFromSemitones[3] = { 7.f, 12.f, 19.f };
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> chordBlendSmoother{1.f};

  uint32_t rngState = 2463534242u;

  std::atomic<bool> soundEnabled{true};

  static float semitoneRatio(float semitones);
};