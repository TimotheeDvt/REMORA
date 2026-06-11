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

    // Low-pass of gravity vector in sensor frame, derived from Euler angles.
    // Used only as a fallback; primary spin detection uses the quaternion.
    float gravX_ = 0.f, gravY_ = 0.f, gravZ_ = 1.f;

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

    // Spin plane detection
    // smoothedPlaneRatio_: 0 = purely horizontal, 1 = purely vertical.
    // Committed state flips only past a hysteresis band around 0.5.
    float smoothedPlaneRatio_  = 0.5f;
    bool  committedIsVertical_ = false;
    static constexpr float kPlaneAttackCoef      = 0.04f;  // slow to commit to new plane
    static constexpr float kPlaneReleaseCoef     = 0.12f;  // faster to release
    static constexpr float kPlaneSwitchHysteresis = 0.15f; // must pass 0.5 ± 0.15

    // Spin direction detection
    // Horizontal: smoothed sign of g_world.z
    // Vertical:   smoothed projection onto a stable reference azimuth
    //
    // smoothedHorizDir_: positive = CCW from above
    // smoothedVertDir_:  positive = "forward" direction (reference azimuth)
    float smoothedHorizDir_  = 0.f;
    float smoothedVertDir_   = 0.f;
    float committedSpinDir_  = 1.f; // +1 or -1, shared across both planes
    static constexpr float kDirAttackCoef       = 0.20f; // track strong signals quickly
    static constexpr float kDirReleaseCoef      = 0.03f; // release slowly
    static constexpr float kDirSwitchHysteresis = 0.35f; // must pass ±hysteresis to flip

    // Reference azimuth for vertical spin direction.
    // Set once on first significant vertical spin, then frozen.
    // This makes direction stable even when the performer turns their body.
    float refAzimuthX_       = 1.f;
    float refAzimuthY_       = 0.f;
    bool  refAzimuthSet_     = false;
    static constexpr float kRefAzimuthAdaptCoef = 0.002f; // very slow adaptation

    // Previous committed states, for change detection (debug logging only)
    bool  prevCommittedIsVertical_ = false;
    float prevCommittedSpinDir_    = 1.f;

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

    static inline void safeNormalize2(float& x, float& y) noexcept {
        float len = std::sqrt(x*x + y*y);
        if (len > 1e-6f) { x /= len; y /= len; }
        else             { x = 1.f;  y = 0.f; }
    }

    // Converts a musical interval in semitones into a frequency in Hz.
    static inline float semiToHz(float semi) noexcept {
        return kRootHz * std::pow(2.0f, semi / 12.0f);
    }

    // Rotate a vector by a unit quaternion: v' = q ⊗ v ⊗ q*
    // (pure quaternion sandwich product, expanded for speed)
    static inline void rotateByQuat(
        float qw, float qx, float qy, float qz,
        float vx, float vy, float vz,
        float& ox, float& oy, float& oz) noexcept
    {
        // t = 2 * cross(q.xyz, v)
        float tx = 2.f * (qy * vz - qz * vy);
        float ty = 2.f * (qz * vx - qx * vz);
        float tz = 2.f * (qx * vy - qy * vx);
        // v' = v + qw * t + cross(q.xyz, t)
        ox = vx + qw * tx + (qy * tz - qz * ty);
        oy = vy + qw * ty + (qz * tx - qx * tz);
        oz = vz + qw * tz + (qx * ty - qy * tx);
    }

    int gyroMagToScaleStep(float gyroMag) noexcept;

    // Core spin classification - called only when isMoving
    void updateSpinClassification(const StaffSoundParams& in,
                                  float gwx, float gwy, float gwz,
                                  float gyroMag);
};