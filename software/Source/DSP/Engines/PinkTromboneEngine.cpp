#include "PinkTromboneEngine.h"
#include <cmath>

void PinkTromboneBiquad::prepare(double sampleRate, float frequencyHz, float q) noexcept {
    const float omega = juce::MathConstants<float>::twoPi * frequencyHz / static_cast<float>(sampleRate);
    const float sn = std::sin(omega);
    const float cs = std::cos(omega);
    const float alpha = sn * 0.5f / q;
    const float b0 = 1.0f / (1.0f + alpha);
    a0 = alpha * b0;
    a2 = -alpha * b0;
    b1 = -2.0f * cs * b0;
    b2 = (1.0f - alpha) * b0;
    xm1 = xm2 = ym1 = ym2 = 0.0f;
}

float PinkTromboneBiquad::process(float x) noexcept {
    const float y = a0 * x + a2 * xm2 - b1 * ym1 - b2 * ym2;
    ym2 = ym1; ym1 = y;
    xm2 = xm1; xm1 = x;
    return y;
}

void PinkTromboneEngine::prepare(double sampleRate, int /*samplesPerBlock*/) {
    currentSampleRate = static_cast<float>(sampleRate);
    glottis = std::make_unique<PinkTrombone::Glottis>(sampleRate);
    tract = std::make_unique<PinkTrombone::VocalTract>(sampleRate);
    aspirateFilter.prepare(sampleRate, 500.0f, 0.5f);
    fricativeFilter.prepare(sampleRate, 1000.0f, 0.5f);
}

float PinkTromboneEngine::nextWhiteNoise() noexcept {
    rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
    return static_cast<float>(rngState) * (2.0f / 4294967296.0f) - 1.0f;
}

void PinkTromboneEngine::render(juce::AudioBuffer<float>& buffer, int numSamples) {
    if (buffer.getNumChannels() == 0 || numSamples <= 0)
        return;

    auto* left  = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    const float invN = 1.0f / static_cast<float>(numSamples);
    for (int j = 0; j < numSamples; ++j) {
        const float noise = nextWhiteNoise();
        const float asp = aspirateFilter.process(noise);
        const float fri = fricativeFilter.process(noise);

        const float lambda1 = static_cast<float>(j) * invN;
        const float lambda2 = (static_cast<float>(j) + 0.5f) * invN;

        const float glotOut = glottis->runStep(lambda1, asp);
        const float glotModulator = glottis->getNoiseModulator();

        float vocalOut = 0.0f;
        tract->runStep(glotOut, fri, lambda1, glotModulator);
        vocalOut += tract->getLipOutput() + tract->getNoseOutput();
        tract->runStep(glotOut, fri, lambda2, glotModulator);
        vocalOut += tract->getLipOutput() + tract->getNoseOutput();

        const float sample = vocalOut * 0.125f;
        left[j] += sample;
        if (right) right[j] += sample;
    }

    // Control-rate updates: computed from this block's params, applied via finishBlock() so they
    // take full effect starting next block (blended in meanwhile via the lambda interpolation
    // above) - matches the original's separation of audio-rate and control-rate updates.
    glottis->setTargetFrequency(juce::jlimit(40.0f, 600.0f, params.frequencyHz));
    glottis->setTargetTenseness(juce::jlimit(0.0f, 1.0f, params.tenseness));

    const float tongueIndex = juce::jlimit(0.0f, 1.0f, params.tongueIndexNorm)
        * (tract->tongueIndexUpperBound() - tract->tongueIndexLowerBound()) + tract->tongueIndexLowerBound();
    tract->setRestDiameter(tongueIndex, params.tongueDiameter);

    const float constrictionIndex = juce::jlimit(0.0f, 1.0f, params.constrictionIndexNorm)
        * static_cast<float>(tract->getTractIndexCount());
    tract->setConstriction(constrictionIndex, params.constrictionDiameter, juce::jlimit(0.0f, 1.0f, params.fricativeIntensity));

    glottis->finishBlock();
    tract->finishBlock(static_cast<float>(numSamples) / currentSampleRate);

    buffer.applyGain(juce::jlimit(0.0f, 1.0f, params.level));
}
