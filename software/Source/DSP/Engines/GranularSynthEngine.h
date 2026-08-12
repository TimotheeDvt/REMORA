#pragma once

#include "ISynthEngine.h"
#include "../IMappingStrategy.h"
#include <array>
#include <cstdint>
#include <vector>

// Grain-cloud engine. Has no audio input and bundles no sample assets, so on prepare() it
// synthesizes its own ~2s internal source buffer (a slowly-evolving harmonic bed + light filtered
// noise) and scans grains across that. `level` defaults to 0 (silent) so presets that don't wire
// "sink.granularSynth" leave this engine inaudible.
class GranularSynthEngine : public ISynthEngine {
public:
    void prepare(double sampleRate, int samplesPerBlock) override;
    void render(juce::AudioBuffer<float>& buffer, int numSamples) override;

    void setParams(const GranularSynthParams& p) { params = p; }

private:
    static constexpr int kMaxGrains = 64;
    static constexpr float kSourceDurationSeconds = 2.0f;

    struct Grain {
        bool active = false;
        float position = 0.0f;  // fractional sample index into sourceBuffer
        float rate = 1.0f;      // playback rate; negative plays the grain backward
        int length = 0;         // samples
        int age = 0;            // samples played so far
        float panL = 0.5f, panR = 0.5f;
        float amp = 1.0f;
    };

    double currentSampleRate = 44100.0;
    std::vector<float> sourceBuffer;
    std::array<Grain, kMaxGrains> grains;
    float spawnCounter = 0.0f;
    uint32_t rngState = 998244353u;

    GranularSynthParams params;

    void buildSourceBuffer();
    void spawnGrain();
    float readSource(float position) const;
    float nextRandom01();
};
