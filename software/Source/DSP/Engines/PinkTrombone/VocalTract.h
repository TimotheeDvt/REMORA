#pragma once

#include <array>

// Digital waveguide model of the oral + nasal vocal tract (Kelly-Lochbaum junctions between
// segments of varying cross-sectional area), ported from cutelabnyc/pink-trombone-cpp (MIT),
// itself a port of Neil Thapen's "Pink Trombone" (dood.al/pinktrombone). Fixed at the original's
// default 44-segment resolution - the upstream project also supports resizing the tract, which
// this port doesn't expose.
namespace PinkTrombone {

class VocalTract {
public:
    explicit VocalTract(double sampleRate);

    // glottalOutput/turbulenceNoise are this sample's excitation; lambda blends between this
    // block's old and new junction reflection coefficients. Call twice per output sample (at
    // lambda = j/N and (j+0.5)/N for a block of N samples) - the original 2x-oversamples the
    // junction math to stay stable against the nonlinear glottal excitation.
    void runStep(float glottalOutput, float turbulenceNoise, float lambda, float glottalNoiseModulator) noexcept;

    float getLipOutput() const noexcept { return lipOutput; }
    float getNoseOutput() const noexcept { return noseOutput; }

    // Target tract shape from tongue position ("index", in tract-segment units - see
    // tongueIndexLowerBound()/UpperBound()) and height ("diameter", roughly 1.5-3.5; smaller means
    // narrower/tongue raised toward the palate).
    void setRestDiameter(float tongueIndex, float tongueDiameter);

    // An additional localized constriction on top of the rest shape (fricatives/plosives/nasals).
    // `index` in tract-segment units (see getTractIndexCount()); `diameter` at or above ~3.7 is a
    // no-op (never narrower than the rest shape, so effectively fully open).
    void setConstriction(float index, float diameter, float fricativeIntensity);

    // Advances the tract shape toward its target and recomputes junction reflection coefficients
    // for the next block; `deltaTimeSeconds` is this block's duration.
    void finishBlock(float deltaTimeSeconds);

    int getTractIndexCount() const noexcept { return n; }
    float tongueIndexLowerBound() const noexcept { return static_cast<float>(bladeStart + 2); }
    float tongueIndexUpperBound() const noexcept { return static_cast<float>(tipStart - 3); }

private:
    void addTransient(int position);
    void addTurbulenceNoise(float turbulenceNoise, float glottalNoiseModulator);
    void addTurbulenceNoiseAtIndex(float turbulenceNoise, float index, float diameter, float glottalNoiseModulator);
    void calculateReflections();
    void calculateNoseReflections();
    void processTransients();
    void reshapeTract(float deltaTimeSeconds);

    static constexpr int n = 44; // total oral tract segments
    static constexpr int bladeStart = 10;
    static constexpr int tipStart = 32;
    static constexpr int lipStart = 39;
    static constexpr int noseLength = 28;
    static constexpr int noseStart = n - noseLength + 1; // 17
    static constexpr float noseOffset = 0.8f;
    static constexpr float glottalReflection = 0.75f;
    static constexpr float lipReflection = -0.85f;
    static constexpr float noseFade = 1.0f; // nasal branch is lossless (upstream TRACT_FADE)
    static constexpr float movementSpeedCmPerSec = 15.0f;
    static constexpr int kMaxTransients = 20;

    double sampleRate;

    std::array<float, n> diameter{}, restDiameter{}, targetDiameter{};
    std::array<float, n> R{}, L{}, A{};
    std::array<float, n + 1> reflection{}, newReflection{}, junctionOutputR{}, junctionOutputL{};

    std::array<float, noseLength> noseR{}, noseL{}, noseDiameter{}, noseA{};
    std::array<float, noseLength + 1> noseJunctionOutputR{}, noseJunctionOutputL{}, noseReflection{};

    float reflectionLeft = 0.f, reflectionRight = 0.f, reflectionNose = 0.f;
    float newReflectionLeft = 0.f, newReflectionRight = 0.f, newReflectionNose = 0.f;

    int lastObstruction = -1;
    float velumTarget = 0.01f;

    // Clicks fired when an oral closure releases (e.g. the "t" in a stop consonant) - pooled like
    // GranularSynthEngine's grains, rather than upstream's ever-growing, never-decremented counter.
    struct Transient {
        bool active = false;
        int position = 0;
        float timeAlive = 0.f, lifeTime = 0.2f, strength = 0.3f, exponent = 200.f;
    };
    std::array<Transient, kMaxTransients> transients{};

    float constrictionIndex = 3.0f, constrictionDiameter = 1.0f, fricativeIntensity = 0.0f;

    float lipOutput = 0.0f, noseOutput = 0.0f;
};

} // namespace PinkTrombone
