#include "BozendoMapping.h"
#include "../BoStaffSynth.h"
#include "../../UI/DebugLog.h"
#include <cmath>
#include <algorithm>

constexpr float BozendoMapping::kPentatonicMinor[BozendoMapping::kNumScaleSteps];
constexpr float BozendoMapping::kChordVoicing[3];

inline void BozendoMapping::rotateByQuat(
    float qw, float qx, float qy, float qz,
    float vx, float vy, float vz,
    float& ox, float& oy, float& oz) noexcept
{
    // t = 2 * (q.xyz x v)
    float tx = 2.f * (qy * vz - qz * vy);
    float ty = 2.f * (qz * vx - qx * vz);
    float tz = 2.f * (qx * vy - qy * vx);
    // v' = v + qw*t + q.xyz × t
    ox = vx + qw * tx + (qy * tz - qz * ty);
    oy = vy + qw * ty + (qz * tx - qx * tz);
    oz = vz + qw * tz + (qx * ty - qy * tx);
}

void BozendoMapping::prepare(double sampleRate) {
    debug.print.green("BozendoMapping prepared at sample rate:", sampleRate);
    sampleRate_ = sampleRate;
    dt_         = 1.0f / 100.0f;

    gravX_ = 0.f; gravY_ = 0.f; gravZ_ = 1.f;
    velX_  = 0.f; velY_  = 0.f; velZ_  = 0.f;

    weightEnvelope_  = 0.f;
    prevGyroMag_     = 0.f;
    suddenness_      = 0.f;
    suddennessEnv_   = 0.f;

    prevGyroDirX_ = 0.f; prevGyroDirY_ = 0.f; prevGyroDirZ_ = 1.f;
    axisFocus_    = 1.f;

    prevDynAccelMag_ = 0.f;
    flowBound_       = 0.f;

    currentScaleStep_  = 0;
    smoothedGyroMag_   = 0.f;
    noiseEnvelope_     = 0.f;
    outGainSmoothed_   = 0.f;
    lastTimestampTicks_ = 0;

    for (int i = 0; i < 3; ++i) { tipX_[i] = 1.f; tipY_[i] = 0.f; tipZ_[i] = 0.f; }

    smoothedAxX_ = 0.f;
    smoothedAxY_ = 0.f;
    smoothedAxZ_ = 1.f;

    committedIsVertical_ = false;
    committedSpinDir_  = 1.f;
    refAzimuthX_   = 1.f;
    refAzimuthY_   = 0.f;
    refAzimuthSet_ = false;
    prevCommittedIsVertical_ = false;
    prevCommittedSpinDir_    = 1.f;

    peakEnvelope_   = 0.f;
    prevAxialAccel_ = 0.f;
    axialJerk_      = 0.f;
    axialJerkEnv_   = 0.f;
}

int BozendoMapping::gyroMagToScaleStep(float gyroMag) noexcept {
    float clamped = juce::jlimit(kGyroFloor, kGyroCeiling, gyroMag);
    float norm = (clamped - kGyroFloor) / (kGyroCeiling - kGyroFloor);
    float floatStep = norm * static_cast<float>(kNumScaleSteps - 1);

    float hysteresisNorm = kScaleHysteresis / (kGyroCeiling - kGyroFloor);
    float hysteresisSteps = hysteresisNorm * static_cast<float>(kNumScaleSteps - 1);

    int targetStep = juce::jlimit(0, kNumScaleSteps - 1,
                                  static_cast<int>(floatStep + 0.5f));
    if (targetStep > currentScaleStep_) {
        if (floatStep < static_cast<float>(currentScaleStep_) + 0.5f + hysteresisSteps)
            targetStep = currentScaleStep_;
    } else if (targetStep < currentScaleStep_) {
        if (floatStep > static_cast<float>(currentScaleStep_) - 0.5f - hysteresisSteps)
            targetStep = currentScaleStep_;
    }
    return targetStep;
}

void BozendoMapping::updateSpinClassification(float axX, float axY, float axZ)
{
    smoothedAxX_ = onePole(smoothedAxX_, axX, kAxisSmoothCoef);
    smoothedAxY_ = onePole(smoothedAxY_, axY, kAxisSmoothCoef);
    smoothedAxZ_ = onePole(smoothedAxZ_, axZ, kAxisSmoothCoef);

    float perpMag = std::sqrt(smoothedAxX_ * smoothedAxX_ + smoothedAxY_ * smoothedAxY_);
    float paraMag = std::abs(smoothedAxZ_);

    committedIsVertical_ = perpMag > paraMag;

    if (committedIsVertical_) {
        if (perpMag > 1e-3f) {
            if (!refAzimuthSet_) {
                refAzimuthX_ = smoothedAxX_ / perpMag;
                refAzimuthY_ = smoothedAxY_ / perpMag;
                refAzimuthSet_ = true;
            }
            float dot = (smoothedAxX_ / perpMag) * refAzimuthX_
                      + (smoothedAxY_ / perpMag) * refAzimuthY_;
            committedSpinDir_ = (dot >= 0.f) ? 1.f : -1.f;
        }
    } else {
        committedSpinDir_ = (smoothedAxZ_ >= 0.f) ? 1.f : -1.f;
    }

    if (committedIsVertical_ != prevCommittedIsVertical_ ||
        committedSpinDir_    != prevCommittedSpinDir_) {
        prevCommittedIsVertical_ = committedIsVertical_;
        prevCommittedSpinDir_    = committedSpinDir_;
        debug.print.cyan(
            committedIsVertical_ ? "VERTICAL" : "HORIZONTAL",
            committedSpinDir_ > 0.f ? "CCW/FWD" : "CW/BWD"
        );
    }
}

void BozendoMapping::process(const StaffSoundParams& in, MappingOutput& out) {

    // dt
    // high-res ticks -> sub-ms accuracy, no platform 1ms quantisation
    {
        const juce::int64 nowTicks = juce::Time::getHighResolutionTicks();
        if (lastTimestampTicks_ != 0) {
            double elapsedSec = juce::Time::highResolutionTicksToSeconds(nowTicks - lastTimestampTicks_);
            if (elapsedSec > 0.0001 && elapsedSec < 0.2)
                dt_ = static_cast<float>(elapsedSec);
        }
        lastTimestampTicks_ = nowTicks;
    }

    // tip = rotate({1,0,0}, q) in world frame
    float tx, ty, tz;
    rotateByQuat(in.qw, in.qx, in.qy, in.qz, 1.f, 0.f, 0.f, tx, ty, tz);

    // shift ring buffer: [2]=oldest, [1]=mid, [0]=newest
    tipX_[2] = tipX_[1]; tipX_[1] = tipX_[0]; tipX_[0] = tx;
    tipY_[2] = tipY_[1]; tipY_[1] = tipY_[0]; tipY_[0] = ty;
    tipZ_[2] = tipZ_[1]; tipZ_[1] = tipZ_[0]; tipZ_[0] = tz;

    // v_mean = (p[0] - p[2]) / 2   (central difference, 1-frame latency)
    // axis   = p[1] x v_mean       (rotation axis at midpoint)
    float vmx = (tipX_[0] - tipX_[2]) * 0.5f;
    float vmy = (tipY_[0] - tipY_[2]) * 0.5f;
    float vmz = (tipZ_[0] - tipZ_[2]) * 0.5f;

    float axX = tipY_[1] * vmz - tipZ_[1] * vmy;
    float axY = tipZ_[1] * vmx - tipX_[1] * vmz;
    float axZ = tipX_[1] * vmy - tipY_[1] * vmx;

    // gravity in sensor frame via q* x (0,0,1) x q
    {
        float gx_t, gy_t, gz_t;
        rotateByQuat(in.qw, -in.qx, -in.qy, -in.qz, 0.f, 0.f, 1.f, gx_t, gy_t, gz_t);
        constexpr float kGravAlpha = 0.10f;  // faster than before: TC ≈ 90ms
        gravX_ = onePole(gravX_, gx_t, kGravAlpha);
        gravY_ = onePole(gravY_, gy_t, kGravAlpha);
        gravZ_ = onePole(gravZ_, gz_t, kGravAlpha);
    }

    // dynamic accel
    const float dynAX = in.ax - gravX_;
    const float dynAY = in.ay - gravY_;
    const float dynAZ = in.az - gravZ_;
    const float dynAccelMag = std::sqrt(dynAX*dynAX + dynAY*dynAY + dynAZ*dynAZ);

    // velocity integration (Laban Weight)
    {
        constexpr float kG = 9.81f;
        const float decayCoef = std::exp(-(dt_ * 1000.0f / kVelDecayHalfLifeMs) * 0.693147f);
        velX_ = (velX_ + dynAX * kG * dt_) * decayCoef;
        velY_ = (velY_ + dynAY * kG * dt_) * decayCoef;
        velZ_ = (velZ_ + dynAZ * kG * dt_) * decayCoef;
    }
    const float velMag = std::sqrt(velX_*velX_ + velY_*velY_ + velZ_*velZ_);

    // gyro magnitude
    const float gyroMag = std::sqrt(in.gx*in.gx + in.gy*in.gy + in.gz*in.gz);
    smoothedGyroMag_ = onePole(smoothedGyroMag_, gyroMag, kGyroSmoothCoef);

    // peak detection
    // A thrust/peak = strong linear acceleration along the staff long axis
    // while rotation is low.
    // staff_world = tip = (tx, ty, tz) already computed above.
    // dyn_world = rotate(dynAccel_sensor, q)
    // axial = dot(dyn_world, staff_world)
    {
        float dwx, dwy, dwz;
        rotateByQuat(in.qw, in.qx, in.qy, in.qz, dynAX, dynAY, dynAZ, dwx, dwy, dwz);

        // dot(dyn_world, staff_world) - positive = thrust along tip direction
        float axialAccel = dwx * tx + dwy * ty + dwz * tz;
        float currentAxialAccelAbs = std::abs(axialAccel);

        // d/dt of axial acceleration (onset detector)
        axialJerk_ = (currentAxialAccelAbs - prevAxialAccel_) / (dt_ + 1e-6f);

        // Smooth positive jerk only (onset, not offset)
        float posJerk = juce::jlimit(0.f, 1.f, std::max(0.f, axialJerk_) / 200.f);
        axialJerkEnv_ = (posJerk > axialJerkEnv_)
            ? onePole(axialJerkEnv_, posJerk, 0.7f)       // fast attack
            : onePole(axialJerkEnv_, posJerk, kPeakJerkSmoothCoef);

        // Gate: must have significant axial accel AND low rotation
        bool isThrust = (currentAxialAccelAbs > kPeakAxialThresh) && (gyroMag < kPeakMaxGyro);

        if (isThrust) {
            float strength = juce::jlimit(0.f, 1.f,
                (currentAxialAccelAbs - kPeakAxialThresh) / (kPeakAxialThresh * 3.f));
            // Add jerk-weighted onset punch
            float onset = strength + axialJerkEnv_ * 0.5f;
            peakEnvelope_ = juce::jlimit(0.f, 1.f, peakEnvelope_ + onset * 0.4f);

            debug.print.yellow(peakEnvelope_, kPeakAxialThresh);
            if (peakEnvelope_ > 0.7f && prevAxialAccel_ <= kPeakAxialThresh)
                debug.print.magenta("THRUST peak | axial:", currentAxialAccelAbs, "gyro:", gyroMag);
        }
        prevAxialAccel_ = currentAxialAccelAbs;
        peakEnvelope_ *= kPeakDecayCoef;
    }

    // Laban: WEIGHT
    {
        float target = juce::jlimit(0.0f, 1.0f, velMag * 0.25f);
        weightEnvelope_ = (target > weightEnvelope_)
            ? onePole(weightEnvelope_, target, kWeightAttackCoef)
            : onePole(weightEnvelope_, target, kWeightReleaseCoef);
    }
    const float weight = weightEnvelope_;

    // Laban: TIME
    {
        suddenness_ = (gyroMag - prevGyroMag_) / (dt_ + 1e-6f);
        prevGyroMag_ = gyroMag;
        float posSudden = juce::jlimit(0.0f, 1.0f,
                          std::max(0.0f, suddenness_) / 800.0f);
        suddennessEnv_ = (posSudden > suddennessEnv_)
            ? onePole(suddennessEnv_, posSudden, 0.6f)
            : onePole(suddennessEnv_, posSudden, kSuddennessSmoothCoef);
    }
    const float suddennessNorm = juce::jlimit(0.0f, 1.0f, suddennessEnv_);

    // Laban: SPACE
    {
        if (gyroMag > 5.0f) {
            float nx = in.gx, ny = in.gy, nz = in.gz;
            safeNormalize3(nx, ny, nz);
            float dot = std::abs(nx * prevGyroDirX_ + ny * prevGyroDirY_ + nz * prevGyroDirZ_);
            axisFocus_ = onePole(axisFocus_, dot, kAxisFocusSmoothCoef);
            prevGyroDirX_ = nx; prevGyroDirY_ = ny; prevGyroDirZ_ = nz;
        } else {
            axisFocus_ = onePole(axisFocus_, 0.5f, 0.02f);
        }
    }
    const float focus = juce::jlimit(0.0f, 1.0f, axisFocus_);

    // Laban: FLOW
    {
        float jerkNorm = juce::jlimit(0.0f, 1.0f,
            std::abs(dynAccelMag - prevDynAccelMag_) / (dt_ + 1e-6f) / 50.0f);
        prevDynAccelMag_ = dynAccelMag;
        flowBound_ = onePole(flowBound_, jerkNorm, kFlowSmoothCoef);
    }
    const float flowBound = juce::jlimit(0.0f, 1.0f, flowBound_);
    const float flowFree  = 1.0f - flowBound;

    // spin classification
    const bool isMoving = smoothedGyroMag_ > kGyroFloor;
    if (isMoving) {
        updateSpinClassification(axX, axY, axZ);
        currentScaleStep_ = gyroMagToScaleStep(smoothedGyroMag_);
    }

    // pitch
    {
        const float baseSemitones = kPentatonicMinor[currentScaleStep_];
        const float planeOffset   = committedIsVertical_ ? 12.0f : 0.0f;
        const float dirOffset     = (committedSpinDir_ > 0.f) ? 0.0f : -7.0f;
        out.rootHz = semiToHz(baseSemitones + planeOffset + dirOffset);
    }

    // chord quality
    if (!committedIsVertical_) {
        if (committedSpinDir_ > 0.f) {
            out.chordSemitones[0] = 4.f; out.chordSemitones[1] = 7.f;  out.chordSemitones[2] = 12.f;
        } else {
            out.chordSemitones[0] = 3.f; out.chordSemitones[1] = 7.f;  out.chordSemitones[2] = 12.f;
        }
    } else {
        if (committedSpinDir_ > 0.f) {
            out.chordSemitones[0] = 4.f; out.chordSemitones[1] = 7.f;  out.chordSemitones[2] = 10.f;
        } else {
            out.chordSemitones[0] = 3.f; out.chordSemitones[1] = 6.f;  out.chordSemitones[2] = 9.f;
        }
    }

    // voices from Weight
    out.numVoices = (weight < 0.25f) ? 1 : (weight < 0.55f) ? 2 : (weight < 0.80f) ? 3 : 4;

    const float melodyGain = isMoving
        ? juce::jlimit(0.2f, 1.0f, 0.2f + smoothedGyroMag_ / kGyroCeiling * 0.8f)
        : 0.0f;
    out.voiceGain[0] = melodyGain;
    out.voiceGain[1] = juce::jlimit(0.0f, 1.0f, (weight - 0.25f) * 4.0f) * 0.8f;
    out.voiceGain[2] = juce::jlimit(0.0f, 1.0f, (weight - 0.55f) * 3.3f) * 0.7f;
    out.voiceGain[3] = juce::jlimit(0.f, 1.f, (weight - 0.80f) * 5.0f) * 0.6f;

    // master gain
    {
        float motionGate = juce::jlimit(0.0f, 1.0f,
            (smoothedGyroMag_ - kGyroFloor * 0.5f) / (kGyroFloor * 1.5f));
        out.masterGain = motionGate * (0.05f + weight * 0.70f);
        // peak punches through the gain gate
        out.masterGain = juce::jlimit(0.f, 1.f, out.masterGain + peakEnvelope_ * 0.6f);
    }

    // timbre from Space
    {
        constexpr float kDirect[6]   = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
        constexpr float kIndirect[6] = { 1.0f, 0.10f, 0.50f, 0.05f, 0.30f, 0.03f };
        for (int p = 0; p < 6; ++p)
            out.partialAmps[p] = kDirect[p] * focus + kIndirect[p] * (1.f - focus);
        out.driveAmt = weight * (1.f - focus * 0.7f) * 2.5f;
        // peak adds bright transient: boost upper partials
        float peakBright = peakEnvelope_ * 0.8f;
        out.partialAmps[3] = juce::jlimit(0.f, 1.f, out.partialAmps[3] + peakBright * 0.5f);
        out.partialAmps[4] = juce::jlimit(0.f, 1.f, out.partialAmps[4] + peakBright * 0.7f);
        out.partialAmps[5] = juce::jlimit(0.f, 1.f, out.partialAmps[5] + peakBright * 0.9f);
        out.driveAmt = juce::jlimit(0.f, 4.f, out.driveAmt + peakEnvelope_ * 2.0f);
    }

    // noise burst from Time + peak
    {
        if (suddennessNorm > 0.3f)
            noiseEnvelope_ = juce::jlimit(0.0f, 1.0f,
                             noiseEnvelope_ + (suddennessNorm - 0.3f) * 1.5f);
        // peak injects its own sharp noise burst
        noiseEnvelope_ = juce::jlimit(0.f, 1.f, noiseEnvelope_ + peakEnvelope_ * 0.5f);
        noiseEnvelope_ *= kNoiseDecayCoef;
    }
    out.noiseAmount = noiseEnvelope_ * 0.4f;
    out.noiseLpCoef = 1.f - (0.2f + suddennessNorm * 0.4f + peakEnvelope_ * 0.4f);

    // vibrato / tremolo from Flow
    out.vibratoDepth  = flowFree  * weight * 0.020f;
    out.vibratoRateHz = 4.5f + smoothedGyroMag_ * 0.005f;
    out.tremoloDepth  = flowBound * weight * 0.30f;
    out.tremoloRateHz = 3.0f + flowBound * 4.0f;

    // stereo pan
    {
        float panBias = 0.f;
        float perpMag = std::sqrt(axX*axX + axY*axY);
        if (perpMag > 1e-3f) {
            float azimuth = std::atan2(axY, axX);
            panBias = juce::jlimit(-0.2f, 0.2f,
                      azimuth / juce::MathConstants<float>::pi * 0.2f);
        }
        const float spread = 0.3f + flowFree * 0.5f;
        out.panL[0] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f + panBias);
        out.panR[0] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f - panBias);
        out.panL[1] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f + panBias);
        out.panR[1] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f - panBias);
        out.panL[2] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f + panBias);
        out.panR[2] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f - panBias);
        out.panL[3] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f + panBias);
        out.panR[3] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f - panBias);
    }
}