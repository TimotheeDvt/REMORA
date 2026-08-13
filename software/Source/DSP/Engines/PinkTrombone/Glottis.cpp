#include "Glottis.h"
#include <JuceHeader.h>
#include <cmath>

namespace PinkTrombone {

namespace {
// Cheap organic wobble for vibrato/tenseness drift: a few incommensurate sines rather than the
// original's simplex noise (which exists purely to make the wobble non-repeating, not for any
// precise spectral property) - much less code for the same perceptual effect.
float wobble(float t) noexcept {
    return 0.6f * std::sin(6.283185f * 0.61f * t + 0.7f)
         + 0.3f * std::sin(6.283185f * 1.13f * t + 2.1f)
         + 0.1f * std::sin(6.283185f * 2.47f * t + 4.4f);
}
} // namespace

Glottis::Glottis(double sampleRateIn) : sampleRate(sampleRateIn) {
    setupWaveform(0.0f);
}

void Glottis::setupWaveform(float lambda) {
    frequency = oldFrequency * (1.0f - lambda) + newFrequency * lambda;
    const float tenseness = oldTenseness * (1.0f - lambda) + newTenseness * lambda;
    Rd = 3.0f * (1.0f - tenseness);
    waveformLength = 1.0f / frequency;

    float rd = Rd;
    if (rd < 0.5f) rd = 0.5f;
    if (rd > 2.7f) rd = 2.7f;

    // Normalized LF model coefficients (time = 1, Ee = 1) - see Fant et al., "A four-parameter model
    // of glottal flow", 1985.
    const float Ra = -0.01f + 0.048f * rd;
    const float Rk = 0.224f + 0.118f * rd;
    const float Rg = (Rk / 4.0f) * (0.5f + 1.2f * Rk) / (0.11f * rd - Ra * (0.5f + 1.2f * Rk));

    const float Ta = Ra;
    const float Tp = 1.0f / (2.0f * Rg);
    const float TeLocal = Tp + Tp * Rk;

    const float epsilonLocal = 1.0f / Ta;
    const float shiftLocal = std::exp(-epsilonLocal * (1.0f - TeLocal));
    const float DeltaLocal = 1.0f - shiftLocal;

    float RHSIntegral = (1.0f / epsilonLocal) * (shiftLocal - 1.0f) + (1.0f - TeLocal) * shiftLocal;
    RHSIntegral = RHSIntegral / DeltaLocal;

    const float totalLowerIntegral = -(TeLocal - Tp) / 2.0f + RHSIntegral;
    const float totalUpperIntegral = -totalLowerIntegral;

    const float omegaLocal = juce::MathConstants<float>::pi / Tp;
    const float s = std::sin(omegaLocal * TeLocal);
    const float y = -juce::MathConstants<float>::pi * s * totalUpperIntegral / (Tp * 2.0f);
    const float z = std::log(y);
    const float alphaLocal = z / (Tp / 2.0f - TeLocal);
    const float E0Local = -1.0f / (s * std::exp(alphaLocal * TeLocal));

    alpha = alphaLocal;
    E0 = E0Local;
    epsilon = epsilonLocal;
    shift = shiftLocal;
    Delta = DeltaLocal;
    Te = TeLocal;
    omega = omegaLocal;
}

float Glottis::getNoiseModulator() const noexcept {
    const float voiced = 0.1f + 0.2f * std::fmax(0.0f, std::sin(juce::MathConstants<float>::twoPi * timeInWaveform / waveformLength));
    return targetTenseness * intensity * voiced + (1.0f - targetTenseness * intensity) * 0.3f;
}

void Glottis::finishBlock() {
    float vibrato = vibratoAmount * std::sin(juce::MathConstants<float>::twoPi * totalTime * vibratoFrequency);
    vibrato += 0.02f * wobble(totalTime * 4.07f);
    vibrato += 0.04f * wobble(totalTime * 2.15f);

    if (targetFrequency > smoothFrequency)
        smoothFrequency = std::fmin(smoothFrequency * 1.1f, targetFrequency);
    if (targetFrequency < smoothFrequency)
        smoothFrequency = std::fmax(smoothFrequency / 1.1f, targetFrequency);
    oldFrequency = newFrequency;
    newFrequency = smoothFrequency * (1.0f + vibrato);

    oldTenseness = newTenseness;
    newTenseness = targetTenseness + 0.1f * wobble(totalTime * 0.46f) + 0.05f * wobble(totalTime * 0.36f);
    // Upstream always-voices (no note-on/off) - intensity ramps 0 -> 1 once at startup and then holds.
    intensity = juce::jlimit(0.0f, 1.0f, intensity + 0.13f);
}

float Glottis::normalizedLFWaveform(float t) const {
    float output;
    if (t > Te)
        output = (-std::exp(-epsilon * (t - Te)) + shift) / Delta;
    else
        output = E0 * std::exp(alpha * t) * std::sin(omega * t);
    return output * intensity * loudness;
}

float Glottis::runStep(float lambda, float noiseSource) noexcept {
    const float timeStep = static_cast<float>(1.0 / sampleRate);
    timeInWaveform += timeStep;
    totalTime += timeStep;
    if (timeInWaveform > waveformLength) {
        timeInWaveform -= waveformLength;
        setupWaveform(lambda);
    }

    float out = normalizedLFWaveform(timeInWaveform / waveformLength);
    float aspiration = intensity * (1.0f - std::sqrt(targetTenseness)) * getNoiseModulator() * noiseSource;
    aspiration *= 0.2f + 0.02f * wobble(totalTime * 1.99f);
    out += aspiration;
    return out;
}

} // namespace PinkTrombone
