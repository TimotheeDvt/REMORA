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

    float gravX_ = 0.f, gravY_ = 0.f, gravZ_ = 1.f;

    float velX_ = 0.f, velY_ = 0.f, velZ_ = 0.f;
    static constexpr float kVelDecayHalfLifeMs = 120.0f;

    float weightEnvelope_     = 0.f;
    static constexpr float kWeightAttackCoef  = 0.15f;
    static constexpr float kWeightReleaseCoef = 0.004f;

    float prevGyroMag_  = 0.f;
    float suddenness_   = 0.f;
    float suddennessEnv_= 0.f;
    static constexpr float kSuddennessSmoothCoef = 0.08f;

    float prevGyroDirX_ = 0.f, prevGyroDirY_ = 0.f, prevGyroDirZ_ = 1.f;
    float axisFocus_    = 1.f;
    static constexpr float kAxisFocusSmoothCoef = 0.05f;

    float prevDynAccelMag_ = 0.f;
    float jerk_            = 0.f;
    float flowBound_       = 0.f;
    static constexpr float kFlowSmoothCoef = 0.07f;

    int   currentScaleStep_   = 0;
    float smoothedGyroMag_    = 0.f;
    static constexpr float kGyroSmoothCoef = 0.12f;
    static constexpr float kScaleHysteresis = 8.0f;

    static constexpr float kGyroFloor   = 30.0f;
    static constexpr float kGyroCeiling = 750.0f;

    static constexpr int kNumScaleSteps = 10;
    static constexpr float kPentatonicMinor[kNumScaleSteps] = {
        -12.f, -9.f, -5.f, -2.f, 0.f,
          3.f,  7.f, 10.f, 12.f, 15.f
    };
    static constexpr float kRootHz = 146.83f;

    float tipX_[3] = {1.f, 1.f, 1.f};
    float tipY_[3] = {0.f, 0.f, 0.f};
    float tipZ_[3] = {0.f, 0.f, 0.f};

    float smoothedAxX_ = 0.f;
    float smoothedAxY_ = 0.f;
    float smoothedAxZ_ = 0.f;

    bool  committedIsVertical_ = false;
    float committedSpinDir_  = 1.f;

    float refAzimuthX_       = 1.f;
    float refAzimuthY_       = 0.f;
    bool  refAzimuthSet_     = false;

    bool  prevCommittedIsVertical_ = false;
    float prevCommittedSpinDir_    = 1.f;

    static constexpr float kChordVoicing[3] = { 7.f, 12.f, 19.f };

    float noiseEnvelope_ = 0.f;
    static constexpr float kNoiseDecayCoef = 0.9985f;

    float outGainSmoothed_ = 0.f;

    static inline float onePole(float y, float x, float coef) noexcept {
        return y + coef * (x - y);
    }

    static inline void safeNormalize3(float& x, float& y, float& z) noexcept {
        float len = std::sqrt(x*x + y*y + z*z);
        if (len > 1e-6f) { x /= len; y /= len; z /= len; }
        else             { x = 1.f; y = 0.f; z = 0.f; }
    }

    static inline void safeNormalize2(float& x, float& y) noexcept {
        float len = std::sqrt(x*x + y*y);
        if (len > 1e-6f) { x /= len; y /= len; }
        else             { x = 1.f;  y = 0.f; }
    }

    static inline float semiToHz(float semi) noexcept {
        return kRootHz * std::pow(2.0f, semi / 12.0f);
    }

    static inline void rotateByQuat(
        float qw, float qx, float qy, float qz,
        float vx, float vy, float vz,
        float& ox, float& oy, float& oz) noexcept
    {
        float tx = 2.f * (qy * vz - qz * vy);
        float ty = 2.f * (qz * vx - qx * vz);
        float tz = 2.f * (qx * vy - qy * vx);
        ox = vx + qw * tx + (qy * tz - qz * ty);
        oy = vy + qw * ty + (qz * tx - qx * tz);
        oz = vz + qw * tz + (qx * ty - qy * tx);
    }

    int gyroMagToScaleStep(float gyroMag) noexcept;

    void updateSpinClassification(float axX, float axY, float axZ);
};