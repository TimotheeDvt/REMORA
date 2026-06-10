#pragma once

#include "../IMappingStrategy.h"
#include <JuceHeader.h>
#include <array>
#include <cmath>

class BozendoMapping : public IMappingStrategy {
public:
    const char* getName() const override { return "Bozendo"; }

    void prepare(double sampleRate) override;
    void process(const StaffSoundParams& in, MappingOutput& out) override;

private:
    double sampleRate_ = 44100.0;
    float  dt_         = 1.0f / 100.0f;
    juce::uint32 lastTimestampMs_ = 0;

    float gravX_ = 0.f, gravY_ = 0.f, gravZ_ = 1.f; // Assume it starts pointing down.

    float velX_ = 0.f, velY_ = 0.f, velZ_ = 0.f;
    static constexpr float kVelDecayHalfLifeMs = 120.0f;

    // Laban State
    float weightEnvelope_     = 0.f;
    static constexpr float kWeightAttackCoef  = 0.15f;  // fast attack
    static constexpr float kWeightReleaseCoef = 0.004f; // very slow release

    float prevGyroMag_  = 0.f;
    float suddenness_   = 0.f; // raw derivative
    float suddennessEnv_= 0.f; // smoothed envelope of positive changes
    static constexpr float kSuddennessSmoothCoef = 0.08f;

    float prevGyroDirX_ = 0.f, prevGyroDirY_ = 0.f, prevGyroDirZ_ = 1.f;
    float axisFocus_    = 1.f; // Smoothed dot product. 1 = direct, 0 = indirect.
    static constexpr float kAxisFocusSmoothCoef = 0.05f;

    float prevDynAccelMag_ = 0.f;
    float jerk_            = 0.f; // raw derivative
    float flowBound_       = 0.f; // smoothed envelope of jerk
    static constexpr float kFlowSmoothCoef = 0.07f;

    // Pitch Mapping State
    int   currentScaleStep_   = 0;
    float smoothedGyroMag_    = 0.f;
    static constexpr float kGyroSmoothCoef = 0.12f;

    static constexpr float kScaleHysteresis = 8.0f;

    static constexpr float kGyroFloor   = 20.0f;
    static constexpr float kGyroCeiling = 400.0f;

    // The scale itself: a D minor pentatonic scale spread over a few octaves.
    static constexpr int kNumScaleSteps = 10;
    static constexpr float kPentatonicMinor[kNumScaleSteps] = {
        0.f, 3.f, 7.f, 10.f, 12.f,  // D3 pentatonic
        15.f, 19.f, 22.f, 24.f, 27.f   // D4 pentatonic
    };
    // The root of our scale is D3.
    static constexpr float kRootHz = 146.83f;

    // Harmony State
    static constexpr float kChordVoicing[3] = { 7.f, 12.f, 19.f }; // 5th, octave, octave+5th

    float noiseEnvelope_ = 0.f;
    static constexpr float kNoiseDecayCoef = 0.9985f;

    float outGainSmoothed_ = 0.f;

    // Helpers

    // A simple one-pole low-pass filter.
    static inline float onePole(float y, float x, float coef) noexcept {
        return y + coef * (x - y);
    }

    // Normalizes a 3D vector, but avoids dividing by zero if the vector is tiny.
    static inline void safeNormalize3(float& x, float& y, float& z) noexcept {
        float len = std::sqrt(x*x + y*y + z*z);
        if (len > 1e-6f) { x /= len; y /= len; z /= len; }
        else             { x = 1.f; y = 0.f; z = 0.f; }
    }

    // Converts a musical interval in semitones into a frequency in Hz.
    static inline float semiToHz(float semi) noexcept {
        return kRootHz * std::pow(2.0f, semi / 12.0f);
    }

    int gyroMagToScaleStep(float gyroMag) noexcept;
};