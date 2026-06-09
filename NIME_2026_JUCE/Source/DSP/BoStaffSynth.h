#pragma once

#include "MathHelpers.h"
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>

// Input parameter bundle (filled by PluginProcessor each block)
struct StaffSoundParams {
    float pitch = 0.f;   // euler pitch, radians -PI/2..+PI/2 (tilt)
    float roll  = 0.f;   // euler roll,  radians -PI..+PI    (twist)
    float yaw   = 0.f;   // euler yaw,   radians -PI..+PI    (horizontal swing)
    float gx = 0.f, gy = 0.f, gz = 0.f;   // gyroscope deg/s
    float ax = 0.f, ay = 0.f, az = 0.f;   // accelerometer g
    bool  isReceivingValidData = false;
};

// One synthesised voice (one note in the chord)
struct SynthVoice {
    static constexpr int kPartials = 6;

    float phase[kPartials]   = {};   // per-partial phase accumulator
    float detune = 0.0f;             // semitone offset from chord root (set per chord type)
    float targetAmp = 0.0f;          // target amplitude (0 = off)

    // Per-partial amplitude smoothers
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> partialAmp[kPartials];

    // Per-voice amplitude smoother (bow envelope)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> voiceAmp;

    // Simple comb delay line for body resonance (~40ms)
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

    // Generate one sample given fundamental freq, sampleRateRecip,
    // partialAmplitudes[6], noiseAmt, xorshift state, and soft-clip drive
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

        // Noise injection (strike transient)
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        float noise = static_cast<float>(rng) * (2.0f / 4294967296.0f) - 1.0f;
        out += noise * noiseAmt;

        // Soft clip (tanh approximation) for warmth/saturation
        if (driveAmt > 0.0f) {
            float d = 1.0f + driveAmt;
            out = (out * d) / (1.0f + std::abs(out * d));
        }

        // Comb filter body resonance: delay ~1800 samples, feedback 0.35
        // Simulates the acoustic body of a hollow instrument
        int readIdx = (combWrite + kCombSize - 1800) % kCombSize;
        float comb  = combBuffer[readIdx];
        combBuffer[combWrite] = out + comb * 0.35f;
        combWrite = (combWrite + 1) % kCombSize;
        out = out * 0.7f + comb * 0.3f;

        return out;
    }
};

// Chord descriptor
struct ChordType {
    const char* name;
    float semitones[4];   // interval offsets in semitones for voices 0-3
    int   numVoices;
};

class BoStaffSynth {
public:
  BoStaffSynth();
  ~BoStaffSynth();

  void prepareToPlay(double sampleRate, int samplesPerBlock);

  // Main processing block where you can map orientation to sound parameters
  void processBlock(juce::AudioBuffer<float> &buffer,
                    const StaffSoundParams& params);

  void setSoundEnabled(bool shouldBeEnabled);
  bool isSoundEnabled() const;

private:
  static constexpr int kNumVoices  = 4;
  static constexpr int kNumChords  = 6;

  // Chord vocabulary — intervals in semitones
  static const ChordType kChords[kNumChords];

  float currentSampleRate  = 44100.f;
  float sampleRateRecip    = 1.f / 44100.f;

  SynthVoice voices[kNumVoices];

  // Master smoothers
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain{0.f};
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bowPressure{0.f};
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rootFreq{110.f};

  // LFO state
  float vibratoPhase  = 0.f;
  float tremoloPhase  = 0.f;
  float chorusPhase[kNumVoices] = {};   // per-voice chorus LFO

  // Strike / noise
  float noiseEnvelope    = 0.f;
  float prevAccelMag     = 0.f;
  float noiseFilterState = 0.f;

  // Chord interpolation
  int   currentChordIdx  = 1;   // index into kChords
  int   targetChordIdx   = 1;
  float chordBlend       = 1.f; // 0→prev, 1→target
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> chordBlendSmoother{1.f};

  // PRNG
  uint32_t rngState = 2463534242u;

  std::atomic<bool> soundEnabled{true};

  // Helpers
  static float semitoneRatio(float semitones);
  static float pitchToRootHz(float pitchRad);
  static int   yawToChordIdx(float yawRad, float rollAbs);
  static void  buildPartialTargets(int chordVoice, float bowP, float rollAbs,
                                   float accelMag, float noiseEnv,
                                   float partialTargets[SynthVoice::kPartials]);
};