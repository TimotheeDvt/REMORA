#include "VocalTract.h"
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>

namespace PinkTrombone {

namespace {
float moveTowards(float current, float target, float amountUp, float amountDown) noexcept {
    if (current < target) return std::min(current + amountUp, target);
    return std::max(current - amountDown, target);
}
} // namespace

VocalTract::VocalTract(double sampleRateIn) : sampleRate(sampleRateIn) {
    constexpr float kBoundA = 7.0f / 44.0f, kDiamA = 0.6f;
    constexpr float kBoundB = 12.0f / 44.0f, kDiamB = 1.1f, kDiamC = 1.5f;
    for (int i = 0; i < n; ++i) {
        float d;
        if (static_cast<float>(i) < kBoundA * static_cast<float>(n) - 0.5f) d = kDiamA;
        else if (static_cast<float>(i) < kBoundB * static_cast<float>(n)) d = kDiamB;
        else d = kDiamC;
        diameter[static_cast<size_t>(i)] = restDiameter[static_cast<size_t>(i)] = targetDiameter[static_cast<size_t>(i)] = d;
    }

    for (int i = 0; i < noseLength; ++i) {
        const float d2 = 2.0f * (static_cast<float>(i) / static_cast<float>(noseLength));
        float d = d2 < 1.0f ? (0.4f + 1.6f * d2) : (0.5f + 1.5f * (2.0f - d2));
        d = std::min(d, 1.9f);
        noseDiameter[static_cast<size_t>(i)] = d;
    }

    calculateReflections();
    calculateNoseReflections();
    noseDiameter[0] = velumTarget;
}

void VocalTract::calculateReflections() {
    for (int i = 0; i < n; ++i)
        A[static_cast<size_t>(i)] = diameter[static_cast<size_t>(i)] * diameter[static_cast<size_t>(i)];
    for (int i = 1; i < n; ++i) {
        reflection[static_cast<size_t>(i)] = newReflection[static_cast<size_t>(i)];
        const float a0 = A[static_cast<size_t>(i - 1)], a1 = A[static_cast<size_t>(i)];
        newReflection[static_cast<size_t>(i)] = (a0 + a1 <= 0.0f) ? 0.999f : (a0 - a1) / (a0 + a1);
    }

    reflectionLeft = newReflectionLeft;
    reflectionRight = newReflectionRight;
    reflectionNose = newReflectionNose;
    const float sum = juce::jmax(1e-6f, A[static_cast<size_t>(noseStart)] + A[static_cast<size_t>(noseStart + 1)] + noseA[0]);
    newReflectionLeft = (2.0f * A[static_cast<size_t>(noseStart)] - sum) / sum;
    newReflectionRight = (2.0f * A[static_cast<size_t>(noseStart + 1)] - sum) / sum;
    newReflectionNose = (2.0f * noseA[0] - sum) / sum;
}

void VocalTract::calculateNoseReflections() {
    for (int i = 0; i < noseLength; ++i)
        noseA[static_cast<size_t>(i)] = noseDiameter[static_cast<size_t>(i)] * noseDiameter[static_cast<size_t>(i)];
    for (int i = 1; i < noseLength; ++i) {
        const float a0 = noseA[static_cast<size_t>(i - 1)], a1 = noseA[static_cast<size_t>(i)];
        noseReflection[static_cast<size_t>(i)] = (a0 - a1) / (a0 + a1);
    }
}

void VocalTract::setRestDiameter(float tongueIndex, float tongueDiameter) {
    for (int i = bladeStart; i < lipStart; ++i) {
        const float t = 1.1f * juce::MathConstants<float>::pi * (tongueIndex - static_cast<float>(i))
                       / static_cast<float>(tipStart - bladeStart);
        const float fixedTongueDiameter = 2.0f + (tongueDiameter - 2.0f) / 1.5f;
        float curve = (1.5f - fixedTongueDiameter + 1.7f) * std::cos(t);
        if (i == bladeStart - 2 || i == lipStart - 1) curve *= 0.8f;
        if (i == bladeStart || i == lipStart - 2) curve *= 0.94f;
        restDiameter[static_cast<size_t>(i)] = 1.5f - curve;
    }
    for (int i = 0; i < n; ++i)
        targetDiameter[static_cast<size_t>(i)] = restDiameter[static_cast<size_t>(i)];
}

void VocalTract::setConstriction(float index, float diam, float fricIntensity) {
    constrictionIndex = index;
    constrictionDiameter = diam;
    fricativeIntensity = fricIntensity;

    velumTarget = 0.01f;
    if (constrictionIndex > static_cast<float>(noseStart) && constrictionDiameter < -noseOffset)
        velumTarget = 0.4f;
    if (constrictionDiameter < -0.85f - noseOffset)
        return;

    float diameter2 = constrictionDiameter - 0.3f;
    if (diameter2 < 0.0f) diameter2 = 0.0f;

    float width;
    if (constrictionIndex < 25.0f) width = 10.0f;
    else if (constrictionIndex >= static_cast<float>(tipStart)) width = 5.0f;
    else width = 10.0f - 5.0f * (constrictionIndex - 25.0f) / (static_cast<float>(tipStart) - 25.0f);

    if (constrictionIndex >= 2.0f && constrictionIndex < static_cast<float>(n) && diameter2 < 3.0f) {
        const int intIndex = static_cast<int>(std::lround(constrictionIndex));
        const int startI = static_cast<int>(-std::ceil(width) - 1.0f);
        for (int i = startI; static_cast<float>(i) < width + 1.0f; ++i) {
            const int idx = intIndex + i;
            if (idx < 0 || idx >= n) continue;
            float relpos = static_cast<float>(idx) - constrictionIndex;
            relpos = std::abs(relpos) - 0.5f;
            float shrink;
            if (relpos <= 0.0f) shrink = 0.0f;
            else if (relpos > width) shrink = 1.0f;
            else shrink = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * relpos / width));
            if (diameter2 < targetDiameter[static_cast<size_t>(idx)])
                targetDiameter[static_cast<size_t>(idx)] = diameter2 + (targetDiameter[static_cast<size_t>(idx)] - diameter2) * shrink;
        }
    }
}

void VocalTract::addTransient(int position) {
    for (auto& t : transients) {
        if (t.active) continue;
        t = Transient{};
        t.active = true;
        t.position = position;
        return;
    }
    // Pool exhausted (20 simultaneous closure-release clicks) - drop it, inaudible either way.
}

void VocalTract::processTransients() {
    for (auto& t : transients) {
        if (!t.active) continue;
        const float amplitude = t.strength * std::pow(2.0f, -t.exponent * t.timeAlive);
        R[static_cast<size_t>(t.position)] += amplitude * 0.5f;
        L[static_cast<size_t>(t.position)] += amplitude * 0.5f;
        t.timeAlive += static_cast<float>(1.0 / (sampleRate * 2.0));
        if (t.timeAlive > t.lifeTime) t.active = false;
    }
}

void VocalTract::addTurbulenceNoise(float turbulenceNoise, float glottalNoiseModulator) {
    if (constrictionIndex < 2.0f || constrictionIndex > static_cast<float>(n)) return;
    if (constrictionDiameter <= 0.0f) return;
    addTurbulenceNoiseAtIndex(0.66f * turbulenceNoise * fricativeIntensity, constrictionIndex, constrictionDiameter, glottalNoiseModulator);
}

void VocalTract::addTurbulenceNoiseAtIndex(float turbulenceNoise, float index, float diam, float glottalNoiseModulator) {
    const int i = static_cast<int>(std::floor(index));
    if (i < 0 || i + 2 >= n) return; // upstream has no such guard; index is clamped to <= n by the caller, so i+2 can overrun
    const float delta = index - static_cast<float>(i);
    turbulenceNoise *= glottalNoiseModulator;
    const float thinness0 = juce::jlimit(0.0f, 1.0f, 8.0f * (0.7f - diam));
    const float openness = juce::jlimit(0.0f, 1.0f, 30.0f * (diam - 0.3f));
    const float noise0 = turbulenceNoise * (1.0f - delta) * thinness0 * openness;
    const float noise1 = turbulenceNoise * delta * thinness0 * openness;
    R[static_cast<size_t>(i + 1)] += noise0 * 0.5f;
    L[static_cast<size_t>(i + 1)] += noise0 * 0.5f;
    R[static_cast<size_t>(i + 2)] += noise1 * 0.5f;
    L[static_cast<size_t>(i + 2)] += noise1 * 0.5f;
}

void VocalTract::reshapeTract(float deltaTimeSeconds) {
    float amount = deltaTimeSeconds * movementSpeedCmPerSec;
    int newLastObstruction = -1;
    for (int i = 0; i < n; ++i) {
        const float d = diameter[static_cast<size_t>(i)];
        const float target = targetDiameter[static_cast<size_t>(i)];
        if (d <= 0.0f) newLastObstruction = i;
        float slowReturn;
        if (i < noseStart) slowReturn = 0.6f;
        else if (i >= tipStart) slowReturn = 1.0f;
        else slowReturn = 0.6f + 0.4f * static_cast<float>(i - noseStart) / static_cast<float>(tipStart - noseStart);
        diameter[static_cast<size_t>(i)] = moveTowards(d, target, slowReturn * amount, 2.0f * amount);
    }
    if (lastObstruction > -1 && newLastObstruction == -1 && noseA[0] < 0.05f)
        addTransient(lastObstruction);
    lastObstruction = newLastObstruction;

    noseDiameter[0] = moveTowards(noseDiameter[0], velumTarget, amount * 0.25f, amount * 0.1f);
    noseA[0] = noseDiameter[0] * noseDiameter[0];
}

void VocalTract::finishBlock(float deltaTimeSeconds) {
    reshapeTract(deltaTimeSeconds);
    calculateReflections();
}

void VocalTract::runStep(float glottalOutput, float turbulenceNoise, float lambda, float glottalNoiseModulator) noexcept {
    processTransients();
    addTurbulenceNoise(turbulenceNoise, glottalNoiseModulator);

    junctionOutputR[0] = L[0] * glottalReflection + glottalOutput;
    junctionOutputL[static_cast<size_t>(n)] = R[static_cast<size_t>(n - 1)] * lipReflection;

    for (int i = 1; i < n; ++i) {
        const float r = reflection[static_cast<size_t>(i)] * (1.0f - lambda) + newReflection[static_cast<size_t>(i)] * lambda;
        const float w = r * (R[static_cast<size_t>(i - 1)] + L[static_cast<size_t>(i)]);
        junctionOutputR[static_cast<size_t>(i)] = R[static_cast<size_t>(i - 1)] - w;
        junctionOutputL[static_cast<size_t>(i)] = L[static_cast<size_t>(i)] + w;
    }

    {
        const int i = noseStart;
        float r = newReflectionLeft * (1.0f - lambda) + reflectionLeft * lambda;
        junctionOutputL[static_cast<size_t>(i)] = r * R[static_cast<size_t>(i - 1)] + (1.0f + r) * (noseL[0] + L[static_cast<size_t>(i)]);
        r = newReflectionRight * (1.0f - lambda) + reflectionRight * lambda;
        junctionOutputR[static_cast<size_t>(i)] = r * L[static_cast<size_t>(i)] + (1.0f + r) * (R[static_cast<size_t>(i - 1)] + noseL[0]);
        r = newReflectionNose * (1.0f - lambda) + reflectionNose * lambda;
        noseJunctionOutputR[0] = r * noseL[0] + (1.0f + r) * (L[static_cast<size_t>(i)] + R[static_cast<size_t>(i - 1)]);
    }

    for (int i = 0; i < n; ++i) {
        R[static_cast<size_t>(i)] = junctionOutputR[static_cast<size_t>(i)] * 0.999f;
        L[static_cast<size_t>(i)] = junctionOutputL[static_cast<size_t>(i + 1)] * 0.999f;
    }
    lipOutput = R[static_cast<size_t>(n - 1)];

    noseJunctionOutputL[static_cast<size_t>(noseLength)] = noseR[static_cast<size_t>(noseLength - 1)] * lipReflection;
    for (int i = 1; i < noseLength; ++i) {
        // Upstream truncates this junction's reflected component to `int` (effectively always 0,
        // silently flattening the nasal branch's internal resonances) - kept as `float` here to
        // match the oral tract's identical junction math a few lines above.
        const float w = noseReflection[static_cast<size_t>(i)] * (noseR[static_cast<size_t>(i - 1)] + noseL[static_cast<size_t>(i)]);
        noseJunctionOutputR[static_cast<size_t>(i)] = noseR[static_cast<size_t>(i - 1)] - w;
        noseJunctionOutputL[static_cast<size_t>(i)] = noseL[static_cast<size_t>(i)] + w;
    }
    for (int i = 0; i < noseLength; ++i) {
        noseR[static_cast<size_t>(i)] = noseJunctionOutputR[static_cast<size_t>(i)] * noseFade;
        noseL[static_cast<size_t>(i)] = noseJunctionOutputL[static_cast<size_t>(i + 1)] * noseFade;
    }
    noseOutput = noseR[static_cast<size_t>(noseLength - 1)];
}

} // namespace PinkTrombone
