#pragma once

#include "ISynthEngine.h"
#include "../IMappingStrategy.h"
#include "PinkTrombone/Glottis.h"
#include "PinkTrombone/VocalTract.h"
#include <cstdint>
#include <memory>

// Simple one-pole-pair bandpass (RBJ cookbook, constant skirt gain), used to color the raw white
// noise into breath/frication noise before it reaches the glottis/tract - ported from
// cutelabnyc/pink-trombone-cpp's Biquad (MIT).
struct PinkTromboneBiquad {
    void prepare(double sampleRate, float frequencyHz, float q) noexcept;
    float process(float x) noexcept;

private:
    float a0 = 0.f, a2 = 0.f, b1 = 0.f, b2 = 0.f;
    float xm1 = 0.f, xm2 = 0.f, ym1 = 0.f, ym2 = 0.f;
};

// Vocal tract physical model: an LF-model glottal source excites a digital-waveguide vocal tract
// (oral + nasal branches). Ported from cutelabnyc/pink-trombone-cpp (MIT), itself a port of Neil
// Thapen's "Pink Trombone". See PinkTrombone/Glottis.* and PinkTrombone/VocalTract.* for the model
// itself; this class is just the per-block driver (control-rate param updates, noise generation,
// the 2x-oversampled per-sample loop) that upstream's plugin/app code provided.
class PinkTromboneEngine : public ISynthEngine {
public:
    void prepare(double sampleRate, int samplesPerBlock) override;
    void render(juce::AudioBuffer<float>& buffer, int numSamples) override;

    void setParams(const PinkTromboneParams& p) { params = p; }

private:
    float currentSampleRate = 44100.f;
    PinkTromboneParams params;

    std::unique_ptr<PinkTrombone::Glottis> glottis;
    std::unique_ptr<PinkTrombone::VocalTract> tract;

    PinkTromboneBiquad aspirateFilter;
    PinkTromboneBiquad fricativeFilter;

    uint32_t rngState = 747796405u;
    float nextWhiteNoise() noexcept;
};
