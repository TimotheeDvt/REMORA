#include "GranularSynthEngine.h"
#include "../MathHelpers.h"
#include <cmath>

namespace {
constexpr float kTwoPi = 6.28318530717958f;
}

void GranularSynthEngine::prepare(double sampleRate, int /*samplesPerBlock*/) {
    currentSampleRate = sampleRate;
    spawnCounter = 0.0f;
    for (auto& g : grains)
        g.active = false;
    buildSourceBuffer();
}

// A base tone with 5 harmonics, each independently amplitude-wobbled at a distinct slow rate so the
// texture never repeats identically, plus a touch of filtered noise for grit - self-contained grain
// fodder, no assets/mic input required.
void GranularSynthEngine::buildSourceBuffer() {
    const int n = static_cast<int>(currentSampleRate * kSourceDurationSeconds);
    sourceBuffer.assign(static_cast<size_t>(juce::jmax(1, n)), 0.0f);

    constexpr float kBaseHz = 110.0f;
    constexpr int kPartials = 5;
    constexpr float kPartialWeights[kPartials] = { 1.0f, 0.5f, 0.33f, 0.22f, 0.15f };
    constexpr float kPartialLfoHz[kPartials]   = { 0.13f, 0.21f, 0.09f, 0.17f, 0.05f };

    uint32_t rng = 1234567u;
    float noiseFilterState = 0.0f;

    for (size_t i = 0; i < sourceBuffer.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(currentSampleRate);

        float sample = 0.0f;
        for (int p = 0; p < kPartials; ++p) {
            const float amp = kPartialWeights[p] * (0.5f + 0.5f * std::sin(kTwoPi * kPartialLfoHz[p] * t + p * 1.7f));
            sample += amp * std::sin(kTwoPi * kBaseHz * static_cast<float>(p + 1) * t);
        }

        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const float white = static_cast<float>(rng) * (2.0f / 4294967296.0f) - 1.0f;
        noiseFilterState = noiseFilterState * 0.97f + white * 0.03f;
        sample += noiseFilterState * 0.15f;

        sourceBuffer[i] = sample * 0.3f;
    }
}

float GranularSynthEngine::nextRandom01() {
    rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
    return static_cast<float>(rngState) * (1.0f / 4294967296.0f);
}

float GranularSynthEngine::readSource(float position) const {
    const int n = static_cast<int>(sourceBuffer.size());
    if (n <= 0)
        return 0.0f;
    float wrapped = std::fmod(position, static_cast<float>(n));
    if (wrapped < 0.0f)
        wrapped += static_cast<float>(n);
    const int i0 = static_cast<int>(wrapped);
    const int i1 = (i0 + 1) % n;
    const float frac = wrapped - static_cast<float>(i0);
    return sourceBuffer[static_cast<size_t>(i0)] * (1.0f - frac) + sourceBuffer[static_cast<size_t>(i1)] * frac;
}

void GranularSynthEngine::spawnGrain() {
    Grain* slot = nullptr;
    for (auto& g : grains) {
        if (!g.active) { slot = &g; break; }
    }
    if (slot == nullptr) // pool exhausted at extreme density - drop this grain
        return;

    const float sourceLen = static_cast<float>(sourceBuffer.size());
    const float posSpray = (nextRandom01() * 2.0f - 1.0f) * params.positionSpray;
    slot->position = (params.positionNorm + posSpray) * sourceLen;

    const float semitoneOffset = params.pitchSemitones + (nextRandom01() * 2.0f - 1.0f) * params.pitchSpray;
    slot->rate = MathHelpers::semitoneRatio(semitoneOffset);
    if (nextRandom01() < params.reverseAmount)
        slot->rate = -slot->rate;

    slot->length = juce::jmax(1, static_cast<int>(params.grainSizeMs * 0.001f * static_cast<float>(currentSampleRate)));
    slot->age = 0;

    const float panPos = juce::jlimit(-1.0f, 1.0f, (nextRandom01() * 2.0f - 1.0f) * params.panSpread);
    const float theta = (panPos * 0.5f + 0.5f) * (juce::MathConstants<float>::pi * 0.5f);
    slot->panL = std::cos(theta);
    slot->panR = std::sin(theta);

    slot->amp = juce::jlimit(0.0f, 1.0f, 1.0f - nextRandom01() * params.ampSpray);
    slot->active = true;
}

void GranularSynthEngine::render(juce::AudioBuffer<float>& buffer, int numSamples) {
    if (buffer.getNumChannels() == 0 || sourceBuffer.empty())
        return;

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    const float densityHz = juce::jmax(0.01f, params.densityHz);
    const float spawnIntervalSamples = static_cast<float>(currentSampleRate) / densityHz;

    for (int i = 0; i < numSamples; ++i) {
        spawnCounter -= 1.0f;
        if (spawnCounter <= 0.0f) {
            spawnGrain();
            spawnCounter += spawnIntervalSamples;
        }

        float mixL = 0.0f, mixR = 0.0f;
        for (auto& g : grains) {
            if (!g.active)
                continue;

            const float s = readSource(g.position);
            const float windowVal = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(g.age) / static_cast<float>(g.length));
            const float sample = s * windowVal * g.amp;
            mixL += sample * g.panL;
            mixR += sample * g.panR;

            g.position += g.rate;
            if (++g.age >= g.length)
                g.active = false;
        }

        left[i] += mixL;
        if (right) right[i] += mixR;
    }

    buffer.applyGain(juce::jlimit(0.0f, 1.0f, params.level));
}
