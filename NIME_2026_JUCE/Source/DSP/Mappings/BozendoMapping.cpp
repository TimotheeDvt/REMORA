#include "BozendoMapping.h"
#include "../BoStaffSynth.h"
#include "../../UI/DebugLog.h"
#include <cmath>
#include <algorithm>

constexpr float BozendoMapping::kPentatonicMinor[BozendoMapping::kNumScaleSteps];
constexpr float BozendoMapping::kChordVoicing[3];

void BozendoMapping::prepare(double sampleRate) {
    debug.print.green("BozendoMapping prepared at sample rate:", sampleRate);
    sampleRate_ = sampleRate;
    dt_         = 1.0f / 100.0f;

    // Reset all state
    gravX_ = 0.f; gravY_ = 0.f; gravZ_ = 1.f;
    velX_  = 0.f; velY_  = 0.f; velZ_  = 0.f;

    weightEnvelope_  = 0.f;
    prevGyroMag_     = 0.f;
    suddenness_      = 0.f;
    suddennessEnv_   = 0.f;

    prevGyroDirX_ = 0.f; prevGyroDirY_ = 0.f; prevGyroDirZ_ = 1.f;
    axisFocus_    = 1.f;

    prevDynAccelMag_ = 0.f;
    jerk_            = 0.f;
    flowBound_       = 0.f;

    currentScaleStep_  = 0;
    currentSpinDir_    = 1.0f;
    isHorizontalPlane_ = false;
    smoothedGyroMag_   = 0.f;
    noiseEnvelope_     = 0.f;
    outGainSmoothed_   = 0.f;
    lastTimestampMs_   = 0;
}

int BozendoMapping::gyroMagToScaleStep(float gyroMag) noexcept {
    // Clamp to our mapped range
    float clamped = juce::jlimit(kGyroFloor, kGyroCeiling, gyroMag);

    // Normalize to [0, 1]
    float norm = (clamped - kGyroFloor) / (kGyroCeiling - kGyroFloor);

    // Map to float step index
    float floatStep = norm * static_cast<float>(kNumScaleSteps - 1);

    // Hysteresis: only cross into a new step if we're sufficiently past the boundary
    float hysteresisNorm = kScaleHysteresis / (kGyroCeiling - kGyroFloor);
    float hysteresisSteps = hysteresisNorm * static_cast<float>(kNumScaleSteps - 1);

    int targetStep = static_cast<int>(floatStep + 0.5f); // round to nearest
    targetStep = juce::jlimit(0, kNumScaleSteps - 1, targetStep);

    // Only move if we're far enough from the current boundary
    if (targetStep > currentScaleStep_) {
        float boundary = static_cast<float>(currentScaleStep_) + 0.5f + hysteresisSteps;
        if (floatStep < boundary) targetStep = currentScaleStep_;
    } else if (targetStep < currentScaleStep_) {
        float boundary = static_cast<float>(currentScaleStep_) - 0.5f - hysteresisSteps;
        if (floatStep > boundary) targetStep = currentScaleStep_;
    }

    return targetStep;
}

void BozendoMapping::process(const StaffSoundParams& in, MappingOutput& out) {

    // Figure out the actual time delta (dt) since the last frame
    {
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        if (lastTimestampMs_ != 0) {
            juce::uint32 elapsed = now - lastTimestampMs_;
            if (elapsed > 0 && elapsed < 200) { // sanity clamp: 5Hz to 5kHz
                dt_ = static_cast<float>(elapsed) * 0.001f;
            }
        }
        lastTimestampMs_ = now;
    }

    //   grav_x =  sin(pitch)
    //   grav_y = -sin(roll) * cos(pitch)
    //   grav_z =  cos(roll) * cos(pitch)
    {
        const float cp = std::cos(in.pitch);
        const float sp = std::sin(in.pitch);
        const float cr = std::cos(in.roll);
        const float sr = std::sin(in.roll);

        // Target gravity vector in sensor frame (1g magnitude)
        float targetGX =  sp;
        float targetGY = -sr * cp;
        float targetGZ =  cr * cp;

        constexpr float kGravAlpha = 0.04f;
        gravX_ = onePole(gravX_, targetGX, kGravAlpha);
        gravY_ = onePole(gravY_, targetGY, kGravAlpha);
        gravZ_ = onePole(gravZ_, targetGZ, kGravAlpha);
    }

    // Dynamic acceleration
    const float dynAX = in.ax - gravX_;
    const float dynAY = in.ay - gravY_;
    const float dynAZ = in.az - gravZ_;
    const float dynAccelMag = std::sqrt(dynAX*dynAX + dynAY*dynAY + dynAZ*dynAZ);

    // acceleration => velocity.
    {
        constexpr float kG = 9.81f;
        const float decayCoef = std::exp(
            -(dt_ * 1000.0f / kVelDecayHalfLifeMs) * 0.693147f
        );

        // Integrate
        velX_ = (velX_ + dynAX * kG * dt_) * decayCoef;
        velY_ = (velY_ + dynAY * kG * dt_) * decayCoef;
        velZ_ = (velZ_ + dynAZ * kG * dt_) * decayCoef;
    }
    const float velMag = std::sqrt(velX_*velX_ + velY_*velY_ + velZ_*velZ_);

    const float gyroMag = std::sqrt(in.gx*in.gx + in.gy*in.gy + in.gz*in.gz);
    smoothedGyroMag_ = onePole(smoothedGyroMag_, gyroMag, kGyroSmoothCoef);

    // Laban: WEIGHT
    {
        float target = juce::jlimit(0.0f, 1.0f, velMag * 0.25f);
        if (target > weightEnvelope_)
            weightEnvelope_ = onePole(weightEnvelope_, target, kWeightAttackCoef);
        else
            weightEnvelope_ = onePole(weightEnvelope_, target, kWeightReleaseCoef);
    }
    const float weight = weightEnvelope_; // [0, 1]

    // Laban: TIME
    {
        suddenness_ = (gyroMag - prevGyroMag_) / (dt_ + 1e-6f); // deg/s²
        prevGyroMag_ = gyroMag;

        // We only care when they speed up, not when they slow down.
        float positiveSuddenness = juce::jlimit(0.0f, 1.0f,
                                   std::max(0.0f, suddenness_) / 800.0f);

        // Smooth it out. Fast attack so it bites immediately, medium decay so it lingers a bit.
        if (positiveSuddenness > suddennessEnv_)
            suddennessEnv_ = onePole(suddennessEnv_, positiveSuddenness, 0.5f);
        else
            suddennessEnv_ = onePole(suddennessEnv_, positiveSuddenness, kSuddennessSmoothCoef);
    }
    const float suddennessNorm = juce::jlimit(0.0f, 1.0f, suddennessEnv_); // [0, 1]

    // Laban: SPACE
    {
        if (gyroMag > 5.0f) { // Only meaningful when actually rotating
            float nx = in.gx, ny = in.gy, nz = in.gz;
            safeNormalize3(nx, ny, nz);

            float dotProduct = std::abs(
                nx * prevGyroDirX_ +
                ny * prevGyroDirY_ +
                nz * prevGyroDirZ_
            );

            // Update the smoother
            axisFocus_ = onePole(axisFocus_, dotProduct, kAxisFocusSmoothCoef);

            prevGyroDirX_ = nx;
            prevGyroDirY_ = ny;
            prevGyroDirZ_ = nz;
        }
        // When not rotating, drift towards a neutral value.
        else {
            axisFocus_ = onePole(axisFocus_, 0.5f, 0.01f);
        }
    }
    const float focus = juce::jlimit(0.0f, 1.0f, axisFocus_); // 1=direct, 0=indirect

    // Laban: FLOW
    {
        float jerkRaw = std::abs(dynAccelMag - prevDynAccelMag_) / (dt_ + 1e-6f);
        prevDynAccelMag_ = dynAccelMag;

        // Normalize
        float jerkNorm = juce::jlimit(0.0f, 1.0f, jerkRaw / 50.0f);
        flowBound_ = onePole(flowBound_, jerkNorm, kFlowSmoothCoef);
    }
    const float flowBound = juce::jlimit(0.0f, 1.0f, flowBound_); // 1=bound, 0=free
    const float flowFree  = 1.0f - flowBound;

    // Plane and Direction Detection
    // Angular velocity around the world Z axis (gravity)
    const float gyroWorldZ = in.gx * gravX_ + in.gy * gravY_ + in.gz * gravZ_;
    // Angular velocity in the horizontal plane (World X/Y)
    const float gyroWorldXY = std::sqrt(std::max(0.0f, gyroMag * gyroMag - gyroWorldZ * gyroWorldZ));

    const bool isHorizontal = std::abs(gyroWorldZ) > gyroWorldXY;
    debug.print.red("isHorizontal", isHorizontal);

    float spinDir = 1.0f;
    if (isHorizontal) {
        spinDir = (gyroWorldZ >= 0.0f) ? 1.0f : -1.0f;
    } else {
        // Vertical spin direction based on dominant local axis (excluding twist on Z)
        float dominantLocal = std::abs(in.gx) > std::abs(in.gy) ? in.gx : in.gy;
        spinDir = (dominantLocal >= 0.0f) ? 1.0f : -1.0f;
    }
    debug.print.green("spinDir", spinDir);

    const bool isMoving = smoothedGyroMag_ > kGyroFloor;
    if (isMoving) {
        currentScaleStep_ = gyroMagToScaleStep(smoothedGyroMag_);
        isHorizontalPlane_ = isHorizontal;
        currentSpinDir_ = spinDir;
    }

    // Convert scale step (0-9) to intensity (0-4)
    int intensityStep = currentScaleStep_ / 2;
    float semitones = kPentatonicMinor[intensityStep] * (currentSpinDir_ > 0.0f ? 1.0f : -1.0f);

    if (isHorizontalPlane_) {
        semitones += 12.0f; // Shift up an octave for horizontal spins
    }

    out.rootHz = semiToHz(semitones);

    out.chordSemitones[0] = kChordVoicing[0]; // 5th above
    out.chordSemitones[1] = kChordVoicing[1]; // octave above
    out.chordSemitones[2] = kChordVoicing[2]; // octave+5th above

    if (weight < 0.25f) {
        out.numVoices = 1;
    } else if (weight < 0.55f) {
        out.numVoices = 2;
    } else if (weight < 0.80f) {
        out.numVoices = 3;
    } else {
        out.numVoices = 4;
    }

    const float melodyGain = isMoving ? juce::jlimit(0.2f, 1.0f,
                              0.2f + smoothedGyroMag_ / kGyroCeiling * 0.8f)
                           : 0.0f;

    out.voiceGain[0] = melodyGain;
    out.voiceGain[1] = juce::jlimit(0.0f, 1.0f, (weight - 0.25f) * 4.0f) * 0.8f;
    out.voiceGain[2] = juce::jlimit(0.0f, 1.0f, (weight - 0.55f) * 3.3f) * 0.7f;
    out.voiceGain[3] = juce::jlimit(0.0f, 1.0f, (weight - 0.80f) * 5.0f) * 0.6f;

    {
        float motionGate = juce::jlimit(0.0f, 1.0f,
                           (smoothedGyroMag_ - kGyroFloor * 0.5f)
                           / (kGyroFloor * 1.5f));

        out.masterGain = motionGate * (0.05f + weight * 0.70f);
    }

    {
        constexpr float kProfileDirect[6]   = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
        constexpr float kProfileIndirect[6] = { 1.0f, 0.10f, 0.50f, 0.05f, 0.30f, 0.03f };

        for (int p = 0; p < 6; ++p) {
            out.partialAmps[p] = kProfileDirect[p] * focus
                               + kProfileIndirect[p] * (1.0f - focus);
        }

        out.driveAmt = weight * (1.0f - focus * 0.7f) * 2.5f;
    }

    {
        if (suddennessNorm > 0.3f) {
            noiseEnvelope_ = juce::jlimit(0.0f, 1.0f,
                             noiseEnvelope_ + (suddennessNorm - 0.3f) * 1.5f);
        }
        noiseEnvelope_ *= kNoiseDecayCoef;
    }

    out.noiseAmount = noiseEnvelope_ * 0.4f;
    out.noiseLpCoef = 1.0f - (0.2f + suddennessNorm * 0.6f);

    out.vibratoDepth  = flowFree * weight * 0.020f;
    out.vibratoRateHz = 4.5f + smoothedGyroMag_ * 0.005f; // slightly faster when spinning faster

    out.tremoloDepth  = flowBound * weight * 0.30f;
    out.tremoloRateHz = 3.0f + flowBound * 4.0f; // bound/interrupted = faster irregular flutter

    {
        float spread = 0.3f + flowFree * 0.5f; // 0.3 tight -> 0.8 wide

        out.panL[0] = 0.5f + spread * 0.10f; out.panR[0] = 0.5f - spread * 0.10f;
        out.panL[1] = 0.5f - spread * 0.10f; out.panR[1] = 0.5f + spread * 0.10f;
        out.panL[2] = 0.5f + spread * 0.40f; out.panR[2] = 0.5f - spread * 0.40f;
        out.panL[3] = 0.5f - spread * 0.40f; out.panR[3] = 0.5f + spread * 0.40f;

        // Clamp to valid range
        for (int v = 0; v < 4; ++v) {
            out.panL[v] = juce::jlimit(0.0f, 1.0f, out.panL[v]);
            out.panR[v] = juce::jlimit(0.0f, 1.0f, out.panR[v]);
        }
    }
}