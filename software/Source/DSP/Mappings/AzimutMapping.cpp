#include "AzimutMapping.h"
#include "../BoStaffSynth.h"
#include "../../UI/DebugLog.h"
#include <cmath>
#include <algorithm>

constexpr float AzimutMapping::kRootSemitoneTable[2][2][2];
constexpr float AzimutMapping::kChordVoicing[3];

AzimutMapping::AzimutMapping()
    : laban_weight_monitor_(addMonitorParam("Drive", "Laban Weight", 0.0f, 1.0f)),
      laban_time_monitor_(addMonitorParam("Noise", "Laban Time", 0.0f, 1.0f)),
      speed_monitor_(addMonitorParam("Gain (Motion Gate)", "Speed", 0.0f, kGyroscopeCeiling)),
      filter_cutoff_monitor_(addMonitorParam("LPF Cutoff", "Spin count", 20.0f, 20000.0f)),
      spin_plane_monitor_(addTextMonitorParam("Root Pitch", "Spin Plane", { "Vertical", "Horizontal" })),
      spin_direction_monitor_(addTextMonitorParam("Root Pitch", "Spin Direction", { "CW", "CCW" })),
      facing_monitor_(addTextMonitorParam("Root Pitch", "Facing", { "North", "East" }))
{
}

void AzimutMapping::prepare(double sample_rate_hz) {
    debug.print.green("AzimutMapping prepared at sample rate:", sample_rate_hz);
    sample_rate_hz_ = sample_rate_hz;
    delta_time_seconds_ = 1.0f / 100.0f;

    gravity_x_ = 0.f; gravity_y_ = 0.f; gravity_z_ = 1.f;
    velocity_x_  = 0.f; velocity_y_  = 0.f; velocity_z_  = 0.f;

    weight_envelope_ = 0.f;
    previous_gyroscope_magnitude_ = 0.f;
    suddenness_ = 0.f;
    suddenness_envelope_ = 0.f;

    previous_gyroscope_direction_x_ = 0.f; previous_gyroscope_direction_y_ = 0.f; previous_gyroscope_direction_z_ = 1.f;
    axis_focus_ = 1.f;

    previous_dynamic_acceleration_magnitude_ = 0.f;
    flow_bound_envelope_ = 0.f;

    smoothed_gyroscope_magnitude_ = 0.f;
    noise_envelope_ = 0.f;
    smoothed_output_gain_ = 0.f;
    current_base_semitones_ = 0.f;
    target_base_semitones_ = 0.f;
    last_timestamp_ticks_ = 0;

    for (int i = 0; i < 3; ++i) {
        tip_position_x_history_[i] = 1.f;
        tip_position_y_history_[i] = 0.f;
        tip_position_z_history_[i] = 0.f;
    }

    smoothed_rotation_axis_x_ = 0.f;
    smoothed_rotation_axis_y_ = 0.f;
    smoothed_rotation_axis_z_ = 1.f;

    smoothed_forward_axis_x_ = 1.f;
    smoothed_forward_axis_y_ = 0.f;

    is_rotation_axis_vertical_ = false;
    rotation_spin_direction_  = 1.f;
    was_rotation_axis_vertical_ = false;
    previous_rotation_spin_direction_ = 1.f;

    is_facing_north_ = true;
    previous_is_facing_north_ = true;

    accumulated_spin_degrees_ = 0.f;
    continuous_spin_count_ = 0;

    smoothed_lpf_cutoff_hz_ = 20000.f;

    axial_thrust_peak_envelope_ = 0.f;
    previous_axial_acceleration_ = 0.f;
    previous_axial_jerk_ = 0.f;
    thrust_cooldown_seconds_ = 0.f;
}

void AzimutMapping::calculateDeltaTime() {
    const juce::int64 current_time_ticks = juce::Time::getHighResolutionTicks();
    if (last_timestamp_ticks_ != 0) {
        double elapsed_seconds = juce::Time::highResolutionTicksToSeconds(current_time_ticks - last_timestamp_ticks_);
        if (elapsed_seconds > 0.0001 && elapsed_seconds < 0.2) {
            delta_time_seconds_ = static_cast<float>(elapsed_seconds);
        }
    }
    last_timestamp_ticks_ = current_time_ticks;
}

void AzimutMapping::updateTipPositionHistory(const StaffSoundParams& input_parameters, float& current_tip_x, float& current_tip_y, float& current_tip_z) {
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, input_parameters.qx, input_parameters.qy, input_parameters.qz, 1.f, 0.f, 0.f, current_tip_x, current_tip_y, current_tip_z);

    tip_position_x_history_[2] = tip_position_x_history_[1]; tip_position_x_history_[1] = tip_position_x_history_[0]; tip_position_x_history_[0] = current_tip_x;
    tip_position_y_history_[2] = tip_position_y_history_[1]; tip_position_y_history_[1] = tip_position_y_history_[0]; tip_position_y_history_[0] = current_tip_y;
    tip_position_z_history_[2] = tip_position_z_history_[1]; tip_position_z_history_[1] = tip_position_z_history_[0]; tip_position_z_history_[0] = current_tip_z;
}

void AzimutMapping::calculateRotationAxisAtMidpoint(float& axis_x, float& axis_y, float& axis_z) {
    // velocity_mean = (position[0] - position[2]) / 2
    float velocity_mean_x = (tip_position_x_history_[0] - tip_position_x_history_[2]) * 0.5f;
    float velocity_mean_y = (tip_position_y_history_[0] - tip_position_y_history_[2]) * 0.5f;
    float velocity_mean_z = (tip_position_z_history_[0] - tip_position_z_history_[2]) * 0.5f;

    // axis = position[1] cross_product velocity_mean (rotation axis at midpoint)
    axis_x = tip_position_y_history_[1] * velocity_mean_z - tip_position_z_history_[1] * velocity_mean_y;
    axis_y = tip_position_z_history_[1] * velocity_mean_x - tip_position_x_history_[1] * velocity_mean_z;
    axis_z = tip_position_x_history_[1] * velocity_mean_y - tip_position_y_history_[1] * velocity_mean_x;
}

void AzimutMapping::updateGravityVector(const StaffSoundParams& input_parameters) {
    float gravity_x_temp, gravity_y_temp, gravity_z_temp;
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, -input_parameters.qx, -input_parameters.qy, -input_parameters.qz, 0.f, 0.f, 1.f, gravity_x_temp, gravity_y_temp, gravity_z_temp);
    constexpr float kGravitySmoothingAlpha = 0.10f;
    gravity_x_ = MathHelpers::applyOnePoleFilter(gravity_x_, gravity_x_temp, kGravitySmoothingAlpha);
    gravity_y_ = MathHelpers::applyOnePoleFilter(gravity_y_, gravity_y_temp, kGravitySmoothingAlpha);
    gravity_z_ = MathHelpers::applyOnePoleFilter(gravity_z_, gravity_z_temp, kGravitySmoothingAlpha);
}

void AzimutMapping::calculateDynamicAcceleration(const StaffSoundParams& input_parameters, float& dynamic_accel_x, float& dynamic_accel_y, float& dynamic_accel_z, float& dynamic_accel_magnitude) {
    dynamic_accel_x = input_parameters.ax - gravity_x_;
    dynamic_accel_y = input_parameters.ay - gravity_y_;
    dynamic_accel_z = input_parameters.az - gravity_z_;
    dynamic_accel_magnitude = std::sqrt(dynamic_accel_x * dynamic_accel_x + dynamic_accel_y * dynamic_accel_y + dynamic_accel_z * dynamic_accel_z);
}

float AzimutMapping::integrateVelocityForLabanWeight(float dynamic_accel_x, float dynamic_accel_y, float dynamic_accel_z) {
    constexpr float kGravityConstant = 9.81f;
    const float decay_coefficient = std::exp(-(delta_time_seconds_ * 1000.0f / kVelocityDecayHalfLifeMilliseconds) * 0.693147f);
    velocity_x_ = (velocity_x_ + dynamic_accel_x * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_y_ = (velocity_y_ + dynamic_accel_y * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_z_ = (velocity_z_ + dynamic_accel_z * kGravityConstant * delta_time_seconds_) * decay_coefficient;

    return std::sqrt(velocity_x_ * velocity_x_ + velocity_y_ * velocity_y_ + velocity_z_ * velocity_z_);
}

void AzimutMapping::detectAxialThrustPeaks(const StaffSoundParams& input_parameters, float dynamic_accel_x, float dynamic_accel_y, float dynamic_accel_z, float tip_x, float tip_y, float tip_z, float gyroscope_magnitude) {
    float dynamic_accel_world_x, dynamic_accel_world_y, dynamic_accel_world_z;
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, input_parameters.qx, input_parameters.qy, input_parameters.qz, dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_world_x, dynamic_accel_world_y, dynamic_accel_world_z);

    float current_axial_acceleration = dynamic_accel_world_x * tip_x + dynamic_accel_world_y * tip_y + dynamic_accel_world_z * tip_z;
    float current_axial_jerk = (current_axial_acceleration - previous_axial_acceleration_) / (delta_time_seconds_ + 1e-6f);

    if (thrust_cooldown_seconds_ > 0.f) {
        thrust_cooldown_seconds_ -= delta_time_seconds_;
    }

    bool is_positive_peak = (current_axial_acceleration > kPeakAxialAccelerationThreshold) && (previous_axial_jerk_ > 0.f && current_axial_jerk <= 0.f);
    bool is_negative_peak = (current_axial_acceleration < -kPeakAxialAccelerationThreshold) && (previous_axial_jerk_ < 0.f && current_axial_jerk >= 0.f);

    bool is_thrust = (is_positive_peak || is_negative_peak) && (gyroscope_magnitude < kPeakMaximumGyroscope);

    if (is_thrust && thrust_cooldown_seconds_ <= 0.f) {
        float current_axial_acceleration_absolute = std::abs(current_axial_acceleration);
        float strength = juce::jlimit(0.f, 1.f, (current_axial_acceleration_absolute - kPeakAxialAccelerationThreshold) / (kPeakAxialAccelerationThreshold * 3.f));

        axial_thrust_peak_envelope_ = juce::jlimit(0.f, 1.f, strength + 0.5f);
        thrust_cooldown_seconds_ = kThrustCooldownDurationSeconds;

        debug.print.magenta("THRUST peak | axial:", current_axial_acceleration_absolute, "gyroscope:", gyroscope_magnitude);
    }

    previous_axial_acceleration_ = current_axial_acceleration;
    previous_axial_jerk_ = current_axial_jerk;
    axial_thrust_peak_envelope_ *= kPeakDecayCoefficient;
}

float AzimutMapping::updateLabanWeight(float velocity_magnitude) {
    float target_weight = juce::jlimit(0.0f, 1.0f, velocity_magnitude * 0.25f);
    if (target_weight > weight_envelope_) {
        weight_envelope_ = MathHelpers::applyOnePoleFilter(weight_envelope_, target_weight, kWeightAttackCoefficient);
    } else {
        weight_envelope_ = MathHelpers::applyOnePoleFilter(weight_envelope_, target_weight, kWeightReleaseCoefficient);
    }
    return weight_envelope_;
}

float AzimutMapping::updateLabanTime(float gyroscope_magnitude) {
    suddenness_ = (gyroscope_magnitude - previous_gyroscope_magnitude_) / (delta_time_seconds_ + 1e-6f);
    previous_gyroscope_magnitude_ = gyroscope_magnitude;
    float positive_suddenness = juce::jlimit(0.0f, 1.0f, std::max(0.0f, suddenness_) / 800.0f);
    if (positive_suddenness > suddenness_envelope_) {
        suddenness_envelope_ = MathHelpers::applyOnePoleFilter(suddenness_envelope_, positive_suddenness, 0.6f);
    } else {
        suddenness_envelope_ = MathHelpers::applyOnePoleFilter(suddenness_envelope_, positive_suddenness, kSuddennessSmoothingCoefficient);
    }
    return juce::jlimit(0.0f, 1.0f, suddenness_envelope_);
}

float AzimutMapping::updateLabanSpace(const StaffSoundParams& input_parameters, float gyroscope_magnitude) {
    if (gyroscope_magnitude > 5.0f) {
        float normal_x = input_parameters.gx;
        float normal_y = input_parameters.gy;
        float normal_z = input_parameters.gz;
        MathHelpers::normalize3DVector(normal_x, normal_y, normal_z);
        float dot_product = std::abs(normal_x * previous_gyroscope_direction_x_ + normal_y * previous_gyroscope_direction_y_ + normal_z * previous_gyroscope_direction_z_);
        axis_focus_ = MathHelpers::applyOnePoleFilter(axis_focus_, dot_product, kAxisFocusSmoothingCoefficient);
        previous_gyroscope_direction_x_ = normal_x;
        previous_gyroscope_direction_y_ = normal_y;
        previous_gyroscope_direction_z_ = normal_z;
    } else {
        axis_focus_ = MathHelpers::applyOnePoleFilter(axis_focus_, 0.5f, 0.02f);
    }
    return juce::jlimit(0.0f, 1.0f, axis_focus_);
}

void AzimutMapping::updateLabanFlow(float dynamic_acceleration_magnitude, float& flow_bound, float& flow_free) {
    float jerk_normalized = juce::jlimit(0.0f, 1.0f, std::abs(dynamic_acceleration_magnitude - previous_dynamic_acceleration_magnitude_) / (delta_time_seconds_ + 1e-6f) / 50.0f);
    previous_dynamic_acceleration_magnitude_ = dynamic_acceleration_magnitude;
    flow_bound_envelope_ = MathHelpers::applyOnePoleFilter(flow_bound_envelope_, jerk_normalized, kFlowSmoothingCoefficient);

    flow_bound = juce::jlimit(0.0f, 1.0f, flow_bound_envelope_);
    flow_free  = 1.0f - flow_bound;
}

bool AzimutMapping::updateSpinClassification(float axis_x, float axis_y, float axis_z) {
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

    bool spin_changed = false;
    constexpr float kEps = 1e-6f;
    if (is_rotation_axis_vertical_ != was_rotation_axis_vertical_ || std::abs(rotation_spin_direction_ - previous_rotation_spin_direction_) > kEps) {
        was_rotation_axis_vertical_ = is_rotation_axis_vertical_;
        previous_rotation_spin_direction_ = rotation_spin_direction_;
        spin_changed = true;
    }
    return spin_changed;
}

bool AzimutMapping::updateFacingClassification(float tip_x, float tip_y) {
    smoothed_forward_axis_x_ = MathHelpers::applyOnePoleFilter(smoothed_forward_axis_x_, tip_x, kForwardAxisSmoothingCoefficient);
    smoothed_forward_axis_y_ = MathHelpers::applyOnePoleFilter(smoothed_forward_axis_y_, tip_y, kForwardAxisSmoothingCoefficient);

    float facing_x, facing_y;
    if (is_rotation_axis_vertical_) {
        facing_x = smoothed_rotation_axis_x_;
        facing_y = smoothed_rotation_axis_y_;
    } else {
        facing_x = smoothed_forward_axis_x_;
        facing_y = smoothed_forward_axis_y_;
    }

    float abs_x = std::abs(facing_x);
    float abs_y = std::abs(facing_y);

    if (is_facing_north_) {
        is_facing_north_ = !(abs_y > abs_x * kFacingHysteresisRatio);
    } else {
        is_facing_north_ = (abs_x > abs_y * kFacingHysteresisRatio);
    }

    bool facing_changed = (is_facing_north_ != previous_is_facing_north_);
    previous_is_facing_north_ = is_facing_north_;
    return facing_changed;
}

void AzimutMapping::applyPitchAndChordToOutput(MappingOutput& mapping_output, const StaffSoundParams& input_parameters) {
    juce::ignoreUnused(input_parameters);

    const int plane_index = is_rotation_axis_vertical_ ? 0 : 1;
    const int spin_index = (rotation_spin_direction_ < 0.f) ? 0 : 1;
    const int facing_index = is_facing_north_ ? 0 : 1;
    target_base_semitones_ = kRootSemitoneTable[plane_index][spin_index][facing_index];

    spin_plane_monitor_.value.store(static_cast<float>(plane_index), std::memory_order_relaxed);
    spin_direction_monitor_.value.store(static_cast<float>(spin_index), std::memory_order_relaxed);
    facing_monitor_.value.store(static_cast<float>(facing_index), std::memory_order_relaxed);

    // Morph speed scales with how fast the staff is moving
    float morph_speed = juce::jlimit(0.005f, 0.2f, smoothed_gyroscope_magnitude_ / 2000.0f);
    current_base_semitones_ = MathHelpers::applyOnePoleFilter(current_base_semitones_, target_base_semitones_, morph_speed);

    // Instead of a chord, we only play octaves of the root note to maintain the same pitch class.
    mapping_output.chordSemitones[0] = 12.f;  // +1 Octave
    mapping_output.chordSemitones[1] = 7.f;  // +5th
    mapping_output.chordSemitones[2] = -12.f; // -1 Octave

    mapping_output.rootHz = MathHelpers::convertSemitonesToHertz(current_base_semitones_, kRootFrequencyHz);
}

void AzimutMapping::applyVoicesToOutput(MappingOutput& mapping_output, float melody_gain) {
    mapping_output.numVoices = 4;
    mapping_output.voiceGain[0] = melody_gain;
    mapping_output.voiceGain[1] = melody_gain * 0.8f;
    mapping_output.voiceGain[2] = melody_gain * 0.7f;
    mapping_output.voiceGain[3] = melody_gain * 0.6f;
}

void AzimutMapping::applyMasterGainToOutput(MappingOutput& mapping_output, float laban_weight, float motion_gate) {
    mapping_output.masterGain = motion_gate * (0.05f + laban_weight * 0.70f);
    mapping_output.masterGain = juce::jlimit(0.f, 1.f, mapping_output.masterGain + axial_thrust_peak_envelope_ * 0.6f);
}

void AzimutMapping::applyTimbreToOutput(MappingOutput& mapping_output, float laban_space_focus, float laban_weight) {
    juce::ignoreUnused(laban_space_focus);
    constexpr float kConstantGain[6] = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
    for (int i = 0; i < 6; ++i) {
        mapping_output.partialAmps[i] = kConstantGain[i];
    }
    mapping_output.driveAmt = 1.0f + laban_weight * 1.0f;

    float peak_brightness = axial_thrust_peak_envelope_ * 0.8f;
    mapping_output.partialAmps[3] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[3] + peak_brightness * 0.5f);
    mapping_output.partialAmps[4] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[4] + peak_brightness * 0.7f);
    mapping_output.partialAmps[5] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[5] + peak_brightness * 0.9f);
    mapping_output.driveAmt = juce::jlimit(0.f, 4.f, mapping_output.driveAmt + axial_thrust_peak_envelope_ * 2.0f);
}

void AzimutMapping::applyNoiseToOutput(MappingOutput& mapping_output, float suddenness_normalized) {
    if (suddenness_normalized > 0.3f) {
        noise_envelope_ = juce::jlimit(0.0f, 1.0f, noise_envelope_ + (suddenness_normalized - 0.3f) * 1.5f);
    }
    noise_envelope_ = juce::jlimit(0.f, 1.f, noise_envelope_ + axial_thrust_peak_envelope_ * 0.5f);
    noise_envelope_ *= kNoiseDecayCoefficient;

    mapping_output.noiseAmount = noise_envelope_ * 0.4f;
    mapping_output.noiseLpCoef = 1.f - (0.2f + suddenness_normalized * 0.4f + axial_thrust_peak_envelope_ * 0.4f);
}

void AzimutMapping::applyModulationToOutput(MappingOutput& mapping_output, float laban_weight, float flow_bound, float flow_free) {
    juce::ignoreUnused(laban_weight, flow_bound, flow_free);
    mapping_output.vibratoDepth  = 0.0f;
    mapping_output.vibratoRateHz = 0.0f;
    mapping_output.tremoloDepth  = 0.0f;
    mapping_output.tremoloRateHz = 0.0f;
}

void AzimutMapping::applyStereoPanToOutput(MappingOutput& mapping_output, float flow_free, float axis_x, float axis_y) {
    juce::ignoreUnused(flow_free, axis_x, axis_y);
    const float spread = 0.5f;
    mapping_output.panL[0] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f);
    mapping_output.panR[0] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f);
    mapping_output.panL[1] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f);
    mapping_output.panR[1] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f);
    mapping_output.panL[2] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f);
    mapping_output.panR[2] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f);
    mapping_output.panL[3] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f);
    mapping_output.panR[3] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f);
}

void AzimutMapping::process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) {
    calculateDeltaTime();

    float tip_x, tip_y, tip_z;
    updateTipPositionHistory(input_parameters, tip_x, tip_y, tip_z);

    float rotation_axis_x, rotation_axis_y, rotation_axis_z;
    calculateRotationAxisAtMidpoint(rotation_axis_x, rotation_axis_y, rotation_axis_z);

    updateGravityVector(input_parameters);

    float dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_magnitude;
    calculateDynamicAcceleration(input_parameters, dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_magnitude);

    float velocity_magnitude = integrateVelocityForLabanWeight(dynamic_accel_x, dynamic_accel_y, dynamic_accel_z);

    const float gyroscope_magnitude = std::sqrt(input_parameters.gx * input_parameters.gx + input_parameters.gy * input_parameters.gy + input_parameters.gz * input_parameters.gz);
    smoothed_gyroscope_magnitude_ = MathHelpers::applyOnePoleFilter(smoothed_gyroscope_magnitude_, gyroscope_magnitude, kGyroscopeSmoothingCoefficient);

    detectAxialThrustPeaks(input_parameters, dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, tip_x, tip_y, tip_z, gyroscope_magnitude);

    float laban_weight = updateLabanWeight(velocity_magnitude);
    float laban_time_suddenness_normalized = updateLabanTime(gyroscope_magnitude);
    float laban_space_focus = updateLabanSpace(input_parameters, gyroscope_magnitude);

    laban_weight_monitor_.value.store(laban_weight, std::memory_order_relaxed);
    laban_time_monitor_.value.store(laban_time_suddenness_normalized, std::memory_order_relaxed);
    speed_monitor_.value.store(smoothed_gyroscope_magnitude_, std::memory_order_relaxed);

    float laban_flow_bound, laban_flow_free;
    updateLabanFlow(dynamic_accel_magnitude, laban_flow_bound, laban_flow_free);

    const bool is_moving = smoothed_gyroscope_magnitude_ > kGyroscopeFloor;
    bool spin_changed = false;
    bool facing_changed = false;
    if (is_moving) {
        spin_changed = updateSpinClassification(rotation_axis_x, rotation_axis_y, rotation_axis_z);
        facing_changed = updateFacingClassification(tip_x, tip_y);

        if (spin_changed) {
            accumulated_spin_degrees_ = 0.f;
            continuous_spin_count_ = 0;
        } else {
            accumulated_spin_degrees_ += gyroscope_magnitude * delta_time_seconds_;
            while (accumulated_spin_degrees_ >= 360.0f) {
                accumulated_spin_degrees_ -= 360.0f;
                continuous_spin_count_++;
                debug.print.magenta("Full circles in current spin:", continuous_spin_count_);
            }
        }
    }

    applyPitchAndChordToOutput(mapping_output, input_parameters);

    if (spin_changed || facing_changed) {
        static constexpr const char* kNoteNames[2][2][2] = {
            // Vertical
            { { "C", "G" },   // CW:  North, East
            { "E", "B" } }, // CCW: North, East
            // Horizontal
            { { "G", "G" },  // CW:  North, East
            { "A", "A" } } // CCW: North, East
        };
        const int plane_index = is_rotation_axis_vertical_ ? 0 : 1;
        const int spin_index = (rotation_spin_direction_ < 0.f) ? 0 : 1;
        const int facing_index = is_facing_north_ ? 0 : 1;
        debug.print.cyan(
            is_rotation_axis_vertical_ ? "VERTICAL" : "HORIZONTAL",
            rotation_spin_direction_ > 0.f ? "COUNTER_CLOCKWISE_OR_FORWARD" : "CLOCKWISE_OR_BACKWARD",
            is_facing_north_ ? "NORTH" : "EAST",
            "| Morphing to:", kNoteNames[plane_index][spin_index][facing_index]
        );
    }

    float melody_gain = is_moving ? 1.0f : 0.0f;
    applyVoicesToOutput(mapping_output, melody_gain);

    float motion_gate = juce::jlimit(0.0f, 1.0f, (smoothed_gyroscope_magnitude_ - kGyroscopeFloor * 0.5f) / (kGyroscopeFloor * 1.5f));
    applyMasterGainToOutput(mapping_output, laban_weight, motion_gate);

    applyTimbreToOutput(mapping_output, laban_space_focus, laban_weight);
    applyNoiseToOutput(mapping_output, laban_time_suddenness_normalized);
    applyModulationToOutput(mapping_output, laban_weight, laban_flow_bound, laban_flow_free);
    applyStereoPanToOutput(mapping_output, laban_flow_free, rotation_axis_x, rotation_axis_y);

    float spin_phase = static_cast<float>(continuous_spin_count_) * 1.5f;
    float sine_val = std::sin(spin_phase);

    float target_lpf_cutoff = juce::jmap(sine_val, -1.0f, 1.0f, 400.0f, 20000.0f);
    smoothed_lpf_cutoff_hz_ = MathHelpers::applyOnePoleFilter(smoothed_lpf_cutoff_hz_, target_lpf_cutoff, 0.03f);

    mapping_output.lpfCutoffHz = smoothed_lpf_cutoff_hz_;
    filter_cutoff_monitor_.value.store(smoothed_lpf_cutoff_hz_, std::memory_order_relaxed);
}
