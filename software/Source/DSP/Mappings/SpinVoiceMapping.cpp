#include "SpinVoiceMapping.h"
#include "../BoStaffSynth.h"
#include "../../UI/DebugLog.h"
#include <cmath>

constexpr float SpinVoiceMapping::kVoiceBaseSemitones[kNumControllableVoices];

SpinVoiceMapping::SpinVoiceMapping()
    : laban_weight_monitor_(addMonitorParam("Drive", "Laban Weight", 0.0f, 1.0f)),
      speed_monitor_(addMonitorParam("Gain", "Speed", 0.0f, StaffMotionAnalyzer::kGyroscopeCeiling)),
      spin_plane_monitor_(addTextMonitorParam("Active Voice", "Spin Plane", { "Vertical", "Horizontal" })),
      spin_direction_monitor_(addTextMonitorParam("Active Voice", "Spin Direction", { "CW", "CCW" })),
      active_voice_monitor_(addTextMonitorParam("Active Voice Index", "Spin Plane + Direction", { "0", "1", "2", "3" }))
{
}

void SpinVoiceMapping::prepare(double sample_rate_hz) {
    debug.print.green("SpinVoiceMapping prepared at sample rate:", sample_rate_hz);
    sample_rate_hz_ = sample_rate_hz;
    motion_.prepare();

    active_voice_index_ = 0;

    for (int v = 0; v < kNumControllableVoices; ++v) {
        voice_semitones_[v] = kVoiceBaseSemitones[v];
        voice_gain_[v] = 0.f;
    }
}

void SpinVoiceMapping::process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) {
    motion_.calculateDeltaTime();

    float tip_x, tip_y, tip_z;
    motion_.updateTipPositionHistory(input_parameters, tip_x, tip_y, tip_z);

    float rotation_axis_x, rotation_axis_y, rotation_axis_z;
    motion_.calculateRotationAxisAtMidpoint(rotation_axis_x, rotation_axis_y, rotation_axis_z);

    motion_.updateGravityVector(input_parameters);

    float dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_magnitude;
    motion_.calculateDynamicAcceleration(input_parameters, dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_magnitude);
    float velocity_magnitude = motion_.integrateVelocityForLabanWeight(dynamic_accel_x, dynamic_accel_y, dynamic_accel_z);
    float laban_weight = motion_.updateLabanWeight(velocity_magnitude);

    const float gyroscope_magnitude = std::sqrt(input_parameters.gx * input_parameters.gx + input_parameters.gy * input_parameters.gy + input_parameters.gz * input_parameters.gz);
    motion_.updateGyroscopeMagnitude(gyroscope_magnitude);

    laban_weight_monitor_.value.store(laban_weight, std::memory_order_relaxed);
    speed_monitor_.value.store(motion_.smoothedGyroscopeMagnitude(), std::memory_order_relaxed);

    const bool is_moving = motion_.smoothedGyroscopeMagnitude() > StaffMotionAnalyzer::kGyroscopeFloor;
    if (is_moving) {
        motion_.updateSpinClassificationByAbsoluteComponent(rotation_axis_x, rotation_axis_y, rotation_axis_z);
    }

    const int plane_index = motion_.isRotationAxisVertical() ? 0 : 1;
    const int spin_index = (motion_.rotationSpinDirection() < 0.f) ? 0 : 1;
    active_voice_index_ = plane_index * 2 + spin_index;

    spin_plane_monitor_.value.store(static_cast<float>(plane_index), std::memory_order_relaxed);
    spin_direction_monitor_.value.store(static_cast<float>(spin_index), std::memory_order_relaxed);
    active_voice_monitor_.value.store(static_cast<float>(active_voice_index_), std::memory_order_relaxed);

    // Only the active voice moves; the other three simply keep whatever
    // pitch/gain they were last given, so switching combos "freezes" them.
    if (is_moving) {
        float speed_normalized = juce::jlimit(0.0f, 1.0f, (motion_.smoothedGyroscopeMagnitude() - StaffMotionAnalyzer::kGyroscopeFloor) / (StaffMotionAnalyzer::kGyroscopeCeiling - StaffMotionAnalyzer::kGyroscopeFloor));
        float target_semitones = kVoiceBaseSemitones[active_voice_index_] + speed_normalized * kSpeedPitchBendSemitones;
        voice_semitones_[active_voice_index_] = MathHelpers::applyOnePoleFilter(voice_semitones_[active_voice_index_], target_semitones, kVoicePitchMorphCoefficient);
    }
    float target_gain = juce::jlimit(0.0f, 1.0f, laban_weight * 1.2f);
    voice_gain_[active_voice_index_] = MathHelpers::applyOnePoleFilter(voice_gain_[active_voice_index_], target_gain, kVoiceGainMorphCoefficient);

    mapping_output.useIndependentVoicePitch = true;
    mapping_output.numVoices = kNumControllableVoices;
    for (int v = 0; v < kNumControllableVoices; ++v) {
        mapping_output.voiceHz[v] = MathHelpers::convertSemitonesToHertz(voice_semitones_[v], kRootFrequencyHz);
        mapping_output.voiceGain[v] = voice_gain_[v];
    }

    mapping_output.rootHz = kRootFrequencyHz;
    mapping_output.chordSemitones[0] = 7.f;
    mapping_output.chordSemitones[1] = 12.f;
    mapping_output.chordSemitones[2] = 19.f;

    // Sustained: once a voice has a gain, it keeps sounding at rest too, so
    // the chord built up across spin states doesn't cut out when you stop.
    mapping_output.masterGain = 1.0f;

    mapping_output.partialAmps[0] = 1.0f;
    mapping_output.partialAmps[1] = 0.5f;
    mapping_output.partialAmps[2] = 0.25f;
    mapping_output.partialAmps[3] = 0.1f;
    mapping_output.partialAmps[4] = 0.05f;
    mapping_output.partialAmps[5] = 0.02f;
    mapping_output.driveAmt = 0.0f;

    mapping_output.vibratoDepth = 0.0f;
    mapping_output.vibratoRateHz = 5.0f;
    mapping_output.tremoloDepth = 0.0f;
    mapping_output.tremoloRateHz = 4.0f;

    mapping_output.noiseAmount = 0.0f;
    mapping_output.noiseLpCoef = 0.5f;

    mapping_output.panL[0] = 0.85f; mapping_output.panR[0] = 0.15f;
    mapping_output.panL[1] = 0.55f; mapping_output.panR[1] = 0.45f;
    mapping_output.panL[2] = 0.45f; mapping_output.panR[2] = 0.55f;
    mapping_output.panL[3] = 0.15f; mapping_output.panR[3] = 0.85f;

    mapping_output.lpfCutoffHz = 20000.0f;
}
