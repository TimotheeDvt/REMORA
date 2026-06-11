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
    smoothedGyroMag_   = 0.f;
    noiseEnvelope_     = 0.f;
    outGainSmoothed_   = 0.f;
    lastTimestampMs_   = 0;

    smoothedPlaneRatio_  = 0.5f;
    committedIsVertical_ = false;

    smoothedHorizDir_  = 0.f;
    smoothedVertDir_   = 0.f;
    committedSpinDir_  = 1.f;

    refAzimuthX_   = 1.f;
    refAzimuthY_   = 0.f;
    refAzimuthSet_ = false;

    prevCommittedIsVertical_ = false;
    prevCommittedSpinDir_    = 1.f;
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

void BozendoMapping::updateSpinClassification(const StaffSoundParams& in,
                                               float gwx, float gwy, float gwz,
                                               float gyroMag)
{
    (void)in;

    const float perpMag = std::sqrt(gwx*gwx + gwy*gwy);
    const float paraMag = std::abs(gwz);

    // ── Plane ratio [0=horizontal, 1=vertical] ───────────────────────────────
    const float rawPlaneRatio = perpMag / (perpMag + paraMag + 1e-6f);
    const float planeAlpha    = (rawPlaneRatio > smoothedPlaneRatio_)
                                ? kPlaneAttackCoef : kPlaneReleaseCoef;
    smoothedPlaneRatio_ = onePole(smoothedPlaneRatio_, rawPlaneRatio, planeAlpha);

    if (!committedIsVertical_) {
        if (smoothedPlaneRatio_ > 0.5f + kPlaneSwitchHysteresis)
            committedIsVertical_ = true;
    } else {
        if (smoothedPlaneRatio_ < 0.5f - kPlaneSwitchHysteresis)
            committedIsVertical_ = false;
    }

    // Horizontal direction: sign of gwz ────────────────────────────────────
    // Soft-clip: x / (|x| + saturation)  maps to ±1 asymptotically
    constexpr float kDirSaturation = 40.f; // deg/s half-saturation point
    const float normHoriz = gwz / (paraMag + kDirSaturation);

    const float horizAlpha = (std::abs(normHoriz) > std::abs(smoothedHorizDir_))
                              ? kDirAttackCoef : kDirReleaseCoef;
    smoothedHorizDir_ = onePole(smoothedHorizDir_, normHoriz, horizAlpha);

    // Vertical direction: projection of rotation axis onto reference azimuth
    float normVert = 0.f;
    if (perpMag > kGyroFloor * 0.5f) {
        float pnx = gwx / perpMag;
        float pny = gwy / perpMag;

        if (!refAzimuthSet_) {
            // Initialise reference to the first strong vertical spin axis
            refAzimuthX_   = pnx;
            refAzimuthY_   = pny;
            refAzimuthSet_ = true;
        } else {
            // Very slow adaptation — only when current observation agrees
            // with the committed direction to avoid self-corruption
            float currentDot = pnx * refAzimuthX_ + pny * refAzimuthY_;
            if ((committedSpinDir_ > 0.f) == (currentDot > 0.f)) {
                float adaptX = (committedSpinDir_ > 0.f) ? pnx : -pnx;
                float adaptY = (committedSpinDir_ > 0.f) ? pny : -pny;
                refAzimuthX_ = onePole(refAzimuthX_, adaptX, kRefAzimuthAdaptCoef);
                refAzimuthY_ = onePole(refAzimuthY_, adaptY, kRefAzimuthAdaptCoef);
                safeNormalize2(refAzimuthX_, refAzimuthY_);
            }
        }

        normVert = pnx * refAzimuthX_ + pny * refAzimuthY_; // dot → [-1, +1]
    }

    const float vertAlpha = (std::abs(normVert) > std::abs(smoothedVertDir_))
                             ? kDirAttackCoef : kDirReleaseCoef;
    smoothedVertDir_ = onePole(smoothedVertDir_, normVert, vertAlpha);

    // Committed direction with hysteresis
    const float activeDir = committedIsVertical_ ? smoothedVertDir_ : smoothedHorizDir_;

    if (committedSpinDir_ > 0.f) {
        if (activeDir < -kDirSwitchHysteresis)
            committedSpinDir_ = -1.f;
    } else {
        if (activeDir >  kDirSwitchHysteresis)
            committedSpinDir_ =  1.f;
    }

    // Debug: log only on state change
    if (committedIsVertical_ != prevCommittedIsVertical_ ||
        committedSpinDir_    != prevCommittedSpinDir_) {
        prevCommittedIsVertical_ = committedIsVertical_;
        prevCommittedSpinDir_    = committedSpinDir_;
        debug.print.cyan(
            committedIsVertical_ ? "  VERTICAL" : "HORIZONTAL",
            committedSpinDir_ > 0.f ? " CCW/FWD" : "  CW/BWD",
            "| ratio:", smoothedPlaneRatio_,
            "hDir:", smoothedHorizDir_,
            "vDir:", smoothedVertDir_
        );
    }
}

void BozendoMapping::process(const StaffSoundParams& in, MappingOutput& out) {

    // 0. dt
    {
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        if (lastTimestampMs_ != 0) {
            juce::uint32 elapsed = now - lastTimestampMs_;
            if (elapsed > 0 && elapsed < 200)
                dt_ = static_cast<float>(elapsed) * 0.001f;
        }
        lastTimestampMs_ = now;
    }

    // Rotate gyro into world frame via calibrated quaternion
    //
    // StaffSoundParams now carries qw/qx/qy/qz (the calibrated quaternion).
    // g_world = q ⊗ g_sensor ⊗ q*
    // This is exact, singularity-free, and requires no Euler reconstruction.
    //
    float gwx, gwy, gwz;
    rotateByQuat(in.qw, in.qx, in.qy, in.qz,
                 in.gx, in.gy, in.gz,
                 gwx, gwy, gwz);

    // Gravity in sensor frame (for dynamic accel subtraction)
    // World +Z = up. Rotate world (0,0,1) by q* to get gravity direction
    // in sensor frame. Low-pass filtered for stability.
    {
        float gx_target, gy_target, gz_target;
        rotateByQuat(in.qw, -in.qx, -in.qy, -in.qz,  // conjugate of q
                     0.f, 0.f, 1.f,
                     gx_target, gy_target, gz_target);

        constexpr float kGravAlpha = 0.04f;
        gravX_ = onePole(gravX_, gx_target, kGravAlpha);
        gravY_ = onePole(gravY_, gy_target, kGravAlpha);
        gravZ_ = onePole(gravZ_, gz_target, kGravAlpha);
    }

    // Dynamic acceleration & velocity
    const float dynAX = in.ax - gravX_;
    const float dynAY = in.ay - gravY_;
    const float dynAZ = in.az - gravZ_;
    const float dynAccelMag = std::sqrt(dynAX*dynAX + dynAY*dynAY + dynAZ*dynAZ);

    {
        constexpr float kG = 9.81f;
        const float decayCoef = std::exp(
            -(dt_ * 1000.0f / kVelDecayHalfLifeMs) * 0.693147f);
        velX_ = (velX_ + dynAX * kG * dt_) * decayCoef;
        velY_ = (velY_ + dynAY * kG * dt_) * decayCoef;
        velZ_ = (velZ_ + dynAZ * kG * dt_) * decayCoef;
    }
    const float velMag = std::sqrt(velX_*velX_ + velY_*velY_ + velZ_*velZ_);

    // Gyro magnitude
    const float gyroMag = std::sqrt(in.gx*in.gx + in.gy*in.gy + in.gz*in.gz);
    smoothedGyroMag_ = onePole(smoothedGyroMag_, gyroMag, kGyroSmoothCoef);

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
            ? onePole(suddennessEnv_, posSudden, 0.5f)
            : onePole(suddennessEnv_, posSudden, kSuddennessSmoothCoef);
    }
    const float suddennessNorm = juce::jlimit(0.0f, 1.0f, suddennessEnv_);

    // Laban: SPACE
    {
        if (gyroMag > 5.0f) {
            float nx = in.gx, ny = in.gy, nz = in.gz;
            safeNormalize3(nx, ny, nz);
            float dotProduct = std::abs(
                nx * prevGyroDirX_ + ny * prevGyroDirY_ + nz * prevGyroDirZ_);
            axisFocus_ = onePole(axisFocus_, dotProduct, kAxisFocusSmoothCoef);
            prevGyroDirX_ = nx; prevGyroDirY_ = ny; prevGyroDirZ_ = nz;
        } else {
            axisFocus_ = onePole(axisFocus_, 0.5f, 0.01f);
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

    // Spin classification
    const bool isMoving = smoothedGyroMag_ > kGyroFloor;
    if (isMoving) {
        updateSpinClassification(in, gwx, gwy, gwz, gyroMag);
        currentScaleStep_ = gyroMagToScaleStep(smoothedGyroMag_);
    }

    // Pitch
    {
        const float baseSemitones = kPentatonicMinor[currentScaleStep_];
        const float planeOffset   = committedIsVertical_ ? 12.0f : 0.0f;
        const float dirOffset     = (committedSpinDir_ > 0.f) ? 0.0f : -7.0f;
        out.rootHz = semiToHz(baseSemitones + planeOffset + dirOffset);
    }

    // Chord quality
    if (!committedIsVertical_) {
        if (committedSpinDir_ > 0.f) {
            out.chordSemitones[0] = 4.f; out.chordSemitones[1] = 7.f;  out.chordSemitones[2] = 12.f; // major
        } else {
            out.chordSemitones[0] = 3.f; out.chordSemitones[1] = 7.f;  out.chordSemitones[2] = 12.f; // minor
        }
    } else {
        if (committedSpinDir_ > 0.f) {
            out.chordSemitones[0] = 4.f; out.chordSemitones[1] = 7.f;  out.chordSemitones[2] = 10.f; // dom7
        } else {
            out.chordSemitones[0] = 3.f; out.chordSemitones[1] = 6.f;  out.chordSemitones[2] = 9.f;  // dim7
        }
    }

    // Voices
    out.numVoices = (weight < 0.25f) ? 1 : (weight < 0.55f) ? 2 : (weight < 0.80f) ? 3 : 4;

    const float melodyGain = isMoving
        ? juce::jlimit(0.2f, 1.0f, 0.2f + smoothedGyroMag_ / kGyroCeiling * 0.8f)
        : 0.0f;
    out.voiceGain[0] = melodyGain;
    out.voiceGain[1] = juce::jlimit(0.0f, 1.0f, (weight - 0.25f) * 4.0f) * 0.8f;
    out.voiceGain[2] = juce::jlimit(0.0f, 1.0f, (weight - 0.55f) * 3.3f) * 0.7f;
    out.voiceGain[3] = juce::jlimit(0.0f, 1.0f, (weight - 0.80f) * 5.0f) * 0.6f;

    // Master gain
    {
        float motionGate = juce::jlimit(0.0f, 1.0f,
            (smoothedGyroMag_ - kGyroFloor * 0.5f) / (kGyroFloor * 1.5f));
        out.masterGain = motionGate * (0.05f + weight * 0.70f);
    }

    // Timbre
    {
        constexpr float kProfileDirect[6]   = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
        constexpr float kProfileIndirect[6] = { 1.0f, 0.10f, 0.50f, 0.05f, 0.30f, 0.03f };
        for (int p = 0; p < 6; ++p)
            out.partialAmps[p] = kProfileDirect[p]   * focus
                               + kProfileIndirect[p] * (1.0f - focus);
        out.driveAmt = weight * (1.0f - focus * 0.7f) * 2.5f;
    }

    // Noise burst
    {
        if (suddennessNorm > 0.3f)
            noiseEnvelope_ = juce::jlimit(0.0f, 1.0f,
                             noiseEnvelope_ + (suddennessNorm - 0.3f) * 1.5f);
        noiseEnvelope_ *= kNoiseDecayCoef;
    }
    out.noiseAmount = noiseEnvelope_ * 0.4f;
    out.noiseLpCoef = 1.0f - (0.2f + suddennessNorm * 0.6f);

    // Vibrato / tremolo
    out.vibratoDepth  = flowFree  * weight * 0.020f;
    out.vibratoRateHz = 4.5f + smoothedGyroMag_ * 0.005f;
    out.tremoloDepth  = flowBound * weight * 0.30f;
    out.tremoloRateHz = 3.0f + flowBound * 4.0f;

    // Stereo pan
    // Pan bias from world-XY azimuth of the rotation axis — continuous and
    // smooth, independent of the committed plane/direction states.
    {
        float panBias = 0.f;
        const float perpMagWorld = std::sqrt(gwx*gwx + gwy*gwy);
        if (perpMagWorld > kGyroFloor * 0.3f) {
            float azimuth = std::atan2(gwy, gwx); // -π to π
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