#include "SpinVoiceMapping.h"
#include "../BoStaffSynth.h"
#include "../../UI/DebugLog.h"
#include <cmath>

constexpr float SpinVoiceMapping::kVoiceBaseSemitones[kNumControllableVoices];

SpinVoiceMapping::SpinVoiceMapping()
    : laban_weight_monitor_(addMonitorParam("Drive", "Laban Weight", 0.0f, 1.0f)),
      speed_monitor_(addMonitorParam("Gain", "Speed", 0.0f, kGyroscopeCeiling)),
      spin_plane_monitor_(addTextMonitorParam("Active Voice", "Spin Plane", { "Vertical", "Horizontal" })),
      spin_direction_monitor_(addTextMonitorParam("Active Voice", "Spin Direction", { "CW", "CCW" })),
      active_voice_monitor_(addTextMonitorParam("Active Voice Index", "Spin Plane + Direction", { "0", "1", "2", "3" }))
{
}

void SpinVoiceMapping::prepare(double sample_rate_hz) {
    debug.print.green("SpinVoiceMapping prepared at sample rate:", sample_rate_hz);
    sample_rate_hz_ = sample_rate_hz;
    delta_time_seconds_ = 1.0f / 100.0f;
    last_timestamp_ticks_ = 0;

    gravity_x_ = 0.f; gravity_y_ = 0.f; gravity_z_ = 1.f;
    velocity_x_ = 0.f; velocity_y_ = 0.f; velocity_z_ = 0.f;
    weight_envelope_ = 0.f;
    smoothed_gyroscope_magnitude_ = 0.f;

    for (int i = 0; i < 3; ++i) {
        tip_position_x_history_[i] = 1.f;
        tip_position_y_history_[i] = 0.f;
        tip_position_z_history_[i] = 0.f;
    }

    smoothed_rotation_axis_x_ = 0.f;
    smoothed_rotation_axis_y_ = 0.f;
    smoothed_rotation_axis_z_ = 1.f;

    is_rotation_axis_vertical_ = false;
    rotation_spin_direction_ = 1.f;
    was_rotation_axis_vertical_ = false;
    previous_rotation_spin_direction_ = 1.f;
    active_voice_index_ = 0;

    for (int v = 0; v < kNumControllableVoices; ++v) {
        voice_semitones_[v] = kVoiceBaseSemitones[v];
        voice_gain_[v] = 0.f;
    }
}

void SpinVoiceMapping::calculateDeltaTime() {
    const juce::int64 current_time_ticks = juce::Time::getHighResolutionTicks();
    if (last_timestamp_ticks_ != 0) {
        double elapsed_seconds = juce::Time::highResolutionTicksToSeconds(current_time_ticks - last_timestamp_ticks_);
        if (elapsed_seconds > 0.0001 && elapsed_seconds < 0.2) {
            delta_time_seconds_ = static_cast<float>(elapsed_seconds);
        }
    }
    last_timestamp_ticks_ = current_time_ticks;
}

void SpinVoiceMapping::updateTipPositionHistory(const StaffSoundParams& input_parameters, float& current_tip_x, float& current_tip_y, float& current_tip_z) {
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, input_parameters.qx, input_parameters.qy, input_parameters.qz, 1.f, 0.f, 0.f, current_tip_x, current_tip_y, current_tip_z);

    tip_position_x_history_[2] = tip_position_x_history_[1]; tip_position_x_history_[1] = tip_position_x_history_[0]; tip_position_x_history_[0] = current_tip_x;
    tip_position_y_history_[2] = tip_position_y_history_[1]; tip_position_y_history_[1] = tip_position_y_history_[0]; tip_position_y_history_[0] = current_tip_y;
    tip_position_z_history_[2] = tip_position_z_history_[1]; tip_position_z_history_[1] = tip_position_z_history_[0]; tip_position_z_history_[0] = current_tip_z;
}

void SpinVoiceMapping::calculateRotationAxisAtMidpoint(float& axis_x, float& axis_y, float& axis_z) {
    // velocity_mean = (position[0] - position[2]) / 2
    float velocity_mean_x = (tip_position_x_history_[0] - tip_position_x_history_[2]) * 0.5f;
    float velocity_mean_y = (tip_position_y_history_[0] - tip_position_y_history_[2]) * 0.5f;
    float velocity_mean_z = (tip_position_z_history_[0] - tip_position_z_history_[2]) * 0.5f;

    // axis = position[1] cross_product velocity_mean (rotation axis at midpoint)
    axis_x = tip_position_y_history_[1] * velocity_mean_z - tip_position_z_history_[1] * velocity_mean_y;
    axis_y = tip_position_z_history_[1] * velocity_mean_x - tip_position_x_history_[1] * velocity_mean_z;
    axis_z = tip_position_x_history_[1] * velocity_mean_y - tip_position_y_history_[1] * velocity_mean_x;
}

void SpinVoiceMapping::updateGravityVector(const StaffSoundParams& input_parameters) {
    float gravity_x_temp, gravity_y_temp, gravity_z_temp;
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, -input_parameters.qx, -input_parameters.qy, -input_parameters.qz, 0.f, 0.f, 1.f, gravity_x_temp, gravity_y_temp, gravity_z_temp);
    constexpr float kGravitySmoothingAlpha = 0.10f;
    gravity_x_ = MathHelpers::applyOnePoleFilter(gravity_x_, gravity_x_temp, kGravitySmoothingAlpha);
    gravity_y_ = MathHelpers::applyOnePoleFilter(gravity_y_, gravity_y_temp, kGravitySmoothingAlpha);
    gravity_z_ = MathHelpers::applyOnePoleFilter(gravity_z_, gravity_z_temp, kGravitySmoothingAlpha);
}

float SpinVoiceMapping::updateLabanWeight(const StaffSoundParams& input_parameters) {
    float dynamic_accel_x = input_parameters.ax - gravity_x_;
    float dynamic_accel_y = input_parameters.ay - gravity_y_;
    float dynamic_accel_z = input_parameters.az - gravity_z_;

    constexpr float kGravityConstant = 9.81f;
    const float decay_coefficient = std::exp(-(delta_time_seconds_ * 1000.0f / kVelocityDecayHalfLifeMilliseconds) * 0.693147f);
    velocity_x_ = (velocity_x_ + dynamic_accel_x * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_y_ = (velocity_y_ + dynamic_accel_y * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_z_ = (velocity_z_ + dynamic_accel_z * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    float velocity_magnitude = std::sqrt(velocity_x_ * velocity_x_ + velocity_y_ * velocity_y_ + velocity_z_ * velocity_z_);

    float target_weight = juce::jlimit(0.0f, 1.0f, velocity_magnitude * 0.25f);
    if (target_weight > weight_envelope_) {
        weight_envelope_ = MathHelpers::applyOnePoleFilter(weight_envelope_, target_weight, kWeightAttackCoefficient);
    } else {
        weight_envelope_ = MathHelpers::applyOnePoleFilter(weight_envelope_, target_weight, kWeightReleaseCoefficient);
    }
    return weight_envelope_;
}

void SpinVoiceMapping::updateSpinClassification(float axis_x, float axis_y, float axis_z) {
    smoothed_rotation_axis_x_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_x_, axis_x, kRotationAxisSmoothingCoefficient);
    smoothed_rotation_axis_y_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_y_, axis_y, kRotationAxisSmoothingCoefficient);
    smoothed_rotation_axis_z_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_z_, axis_z, kRotationAxisSmoothingCoefficient);

    float perpendicular_magnitude = std::sqrt(smoothed_rotation_axis_x_ * smoothed_rotation_axis_x_ + smoothed_rotation_axis_y_ * smoothed_rotation_axis_y_);
    float parallel_magnitude = std::abs(smoothed_rotation_axis_z_);

    is_rotation_axis_vertical_ = perpendicular_magnitude > parallel_magnitude;

    if (is_rotation_axis_vertical_) {
        if (perpendicular_magnitude > 1e-3f) {
            float axis_x_norm = smoothed_rotation_axis_x_ / perpendicular_magnitude;
            float axis_y_norm = smoothed_rotation_axis_y_ / perpendicular_magnitude;
            float reference_component = (std::abs(axis_x_norm) >= std::abs(axis_y_norm)) ? axis_x_norm : axis_y_norm;
            rotation_spin_direction_ = (reference_component >= 0.f) ? 1.f : -1.f;
        }
    } else {
        rotation_spin_direction_ = (smoothed_rotation_axis_z_ >= 0.f) ? 1.f : -1.f;
    }

    was_rotation_axis_vertical_ = is_rotation_axis_vertical_;
    previous_rotation_spin_direction_ = rotation_spin_direction_;
}

void SpinVoiceMapping::process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) {
    calculateDeltaTime();

    float tip_x, tip_y, tip_z;
    updateTipPositionHistory(input_parameters, tip_x, tip_y, tip_z);

    float rotation_axis_x, rotation_axis_y, rotation_axis_z;
    calculateRotationAxisAtMidpoint(rotation_axis_x, rotation_axis_y, rotation_axis_z);

    updateGravityVector(input_parameters);
    float laban_weight = updateLabanWeight(input_parameters);

    const float gyroscope_magnitude = std::sqrt(input_parameters.gx * input_parameters.gx + input_parameters.gy * input_parameters.gy + input_parameters.gz * input_parameters.gz);
    smoothed_gyroscope_magnitude_ = MathHelpers::applyOnePoleFilter(smoothed_gyroscope_magnitude_, gyroscope_magnitude, kGyroscopeSmoothingCoefficient);

    laban_weight_monitor_.value.store(laban_weight, std::memory_order_relaxed);
    speed_monitor_.value.store(smoothed_gyroscope_magnitude_, std::memory_order_relaxed);

    const bool is_moving = smoothed_gyroscope_magnitude_ > kGyroscopeFloor;
    if (is_moving) {
        updateSpinClassification(rotation_axis_x, rotation_axis_y, rotation_axis_z);
    }

    const int plane_index = is_rotation_axis_vertical_ ? 0 : 1;
    const int spin_index = (rotation_spin_direction_ < 0.f) ? 0 : 1;
    active_voice_index_ = plane_index * 2 + spin_index;

    spin_plane_monitor_.value.store(static_cast<float>(plane_index), std::memory_order_relaxed);
    spin_direction_monitor_.value.store(static_cast<float>(spin_index), std::memory_order_relaxed);
    active_voice_monitor_.value.store(static_cast<float>(active_voice_index_), std::memory_order_relaxed);

    // Only the active voice moves; the other three simply keep whatever
    // pitch/gain they were last given, so switching combos "freezes" them.
    if (is_moving) {
        float speed_normalized = juce::jlimit(0.0f, 1.0f, (smoothed_gyroscope_magnitude_ - kGyroscopeFloor) / (kGyroscopeCeiling - kGyroscopeFloor));
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
