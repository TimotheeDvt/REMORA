#pragma once

#include "ISynthEngine.h"
#include "../IMappingStrategy.h"
#include "../MathHelpers.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cstdint>

// Paul Kellet's "economy" 3-pole pink noise filter (-3dB/octave approximation, cheap enough per-voice).
struct PinkNoiseFilter {
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;

    float process(float white) noexcept {
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.2965164f;
        b2 = 0.57000f * b2 + white * 1.0526913f;
        return (b0 + b1 + b2 + white * 0.1848f) * 0.11f;
    }
};

struct SynthVoice {
    static constexpr int kPartials = 6;

    float phase[kPartials]   = {};
    float detune = 0.0f;
    float targetAmp = 0.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> partialAmp[kPartials];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> voiceAmp;

    PinkNoiseFilter pinkFilter;

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
               float noiseAmt, uint32_t& rng, float driveAmt, bool usePinkNoise)
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
        float whiteNoise = static_cast<float>(rng) * (2.0f / 4294967296.0f) - 1.0f;
        float noise = usePinkNoise ? pinkFilter.process(whiteNoise) : whiteNoise;
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

// The original BoStaffSynth voice engine: 4 voices x 6 harmonic partials, chord blend, vibrato/tremolo,
// drive, noise, comb, master lowpass, reverb. Renders into its own buffer, unscaled by master gain -
// SynthManager applies the shared "General Gain" after summing every engine's output.
class AdditiveSynthEngine : public ISynthEngine {
public:
    void prepare(double sampleRate, int samplesPerBlock) override;
    void render(juce::AudioBuffer<float>& buffer, int numSamples) override;

    void setParams(const AdditiveSynthParams& p) { params = p; }

    float getCurrentRootHz() const { return rootFreq.getCurrentValue(); }
    float getCurrentLpfCutoffHz() const { return params.lpfCutoffHz; }

private:
    static constexpr int kNumVoices = 4;

    float currentSampleRate = 44100.f;
    float sampleRateRecip   = 1.f / 44100.f;

    SynthVoice voices[kNumVoices];
    AdditiveSynthParams params;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> rootFreq{110.f};

    float vibratoPhase = 0.f;
    float tremoloPhase = 0.f;
    float chorusPhase[kNumVoices] = {};

    float noiseFilterState = 0.f;

    float prevChordSemitones[3] = { 7.f, 12.f, 19.f };
    float blendFromSemitones[3] = { 7.f, 12.f, 19.f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> chordBlendSmoother{1.f};

    uint32_t rngState = 2463534242u;

    static float semitoneRatio(float semitones);

    juce::dsp::StateVariableTPTFilter<float> masterLowPassFilter;

    juce::dsp::Reverb reverb;
    juce::AudioBuffer<float> reverbWetBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> reverbWetSmoothed{0.f};
};
