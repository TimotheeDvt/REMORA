#include "BozendoMapping2.h"
#include "../BoStaffSynth.h"
#include "../../UI/DebugLog.h"
#include <cmath>
#include <algorithm>

constexpr float BozendoMapping2::kChordVoicing[3];

void BozendoMapping2::prepare(double sample_rate_hz) {
    debug.print.green("BozendoMapping2 prepared at sample rate:", sample_rate_hz);
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

    current_scale_step_ = 0;
    smoothed_gyroscope_magnitude_ = 0.f;
    noise_envelope_ = 0.f;
    smoothed_output_gain_ = 0.f;
    last_timestamp_ticks_ = 0;

    for (int i = 0; i < 3; ++i) {
        tip_position_x_history_[i] = 1.f;
        tip_position_y_history_[i] = 0.f;
        tip_position_z_history_[i] = 0.f;
    }

    smoothed_rotation_axis_x_ = 0.f;
    smoothed_rotation_axis_y_ = 0.f;
    smoothed_rotation_axis_z_ = 1.f;

    is_rotation_axis_vertical_ = false;
    rotation_spin_direction_  = 1.f;
    reference_azimuth_x_   = 1.f;
    reference_azimuth_y_   = 0.f;
    is_reference_azimuth_set_ = false;
    was_rotation_axis_vertical_ = false;
    previous_rotation_spin_direction_ = 1.f;

    axial_thrust_peak_envelope_ = 0.f;
    previous_axial_acceleration_ = 0.f;
    previous_axial_jerk_ = 0.f;
    thrust_cooldown_seconds_ = 0.f;
}

void BozendoMapping2::calculateDeltaTime() {
    const juce::int64 current_time_ticks = juce::Time::getHighResolutionTicks();
    if (last_timestamp_ticks_ != 0) {
        double elapsed_seconds = juce::Time::highResolutionTicksToSeconds(current_time_ticks - last_timestamp_ticks_);
        if (elapsed_seconds > 0.0001 && elapsed_seconds < 0.2) {
            delta_time_seconds_ = static_cast<float>(elapsed_seconds);
        }
    }
    last_timestamp_ticks_ = current_time_ticks;
}

void BozendoMapping2::updateTipPositionHistory(const StaffSoundParams& input_parameters, float& current_tip_x, float& current_tip_y, float& current_tip_z) {
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, input_parameters.qx, input_parameters.qy, input_parameters.qz, 1.f, 0.f, 0.f, current_tip_x, current_tip_y, current_tip_z);

    // shift ring buffer: [2]=oldest, [1]=mid, [0]=newest
    tip_position_x_history_[2] = tip_position_x_history_[1]; tip_position_x_history_[1] = tip_position_x_history_[0]; tip_position_x_history_[0] = current_tip_x;
    tip_position_y_history_[2] = tip_position_y_history_[1]; tip_position_y_history_[1] = tip_position_y_history_[0]; tip_position_y_history_[0] = current_tip_y;
    tip_position_z_history_[2] = tip_position_z_history_[1]; tip_position_z_history_[1] = tip_position_z_history_[0]; tip_position_z_history_[0] = current_tip_z;
}

void BozendoMapping2::calculateRotationAxisAtMidpoint(float& axis_x, float& axis_y, float& axis_z) {
    // velocity_mean = (position[0] - position[2]) / 2 (central difference, 1-frame latency)
    float velocity_mean_x = (tip_position_x_history_[0] - tip_position_x_history_[2]) * 0.5f;
    float velocity_mean_y = (tip_position_y_history_[0] - tip_position_y_history_[2]) * 0.5f;
    float velocity_mean_z = (tip_position_z_history_[0] - tip_position_z_history_[2]) * 0.5f;

    // axis = position[1] cross_product velocity_mean (rotation axis at midpoint)
    axis_x = tip_position_y_history_[1] * velocity_mean_z - tip_position_z_history_[1] * velocity_mean_y;
    axis_y = tip_position_z_history_[1] * velocity_mean_x - tip_position_x_history_[1] * velocity_mean_z;
    axis_z = tip_position_x_history_[1] * velocity_mean_y - tip_position_y_history_[1] * velocity_mean_x;
}

void BozendoMapping2::updateGravityVector(const StaffSoundParams& input_parameters) {
    float gravity_x_temp, gravity_y_temp, gravity_z_temp;
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, -input_parameters.qx, -input_parameters.qy, -input_parameters.qz, 0.f, 0.f, 1.f, gravity_x_temp, gravity_y_temp, gravity_z_temp);
    constexpr float kGravitySmoothingAlpha = 0.10f; // Time Constant approx 90ms
    gravity_x_ = MathHelpers::applyOnePoleFilter(gravity_x_, gravity_x_temp, kGravitySmoothingAlpha);
    gravity_y_ = MathHelpers::applyOnePoleFilter(gravity_y_, gravity_y_temp, kGravitySmoothingAlpha);
    gravity_z_ = MathHelpers::applyOnePoleFilter(gravity_z_, gravity_z_temp, kGravitySmoothingAlpha);
}

void BozendoMapping2::calculateDynamicAcceleration(const StaffSoundParams& input_parameters, float& dynamic_accel_x, float& dynamic_accel_y, float& dynamic_accel_z, float& dynamic_accel_magnitude) {
    dynamic_accel_x = input_parameters.ax - gravity_x_;
    dynamic_accel_y = input_parameters.ay - gravity_y_;
    dynamic_accel_z = input_parameters.az - gravity_z_;
    dynamic_accel_magnitude = std::sqrt(dynamic_accel_x * dynamic_accel_x + dynamic_accel_y * dynamic_accel_y + dynamic_accel_z * dynamic_accel_z);
}

float BozendoMapping2::integrateVelocityForLabanWeight(float dynamic_accel_x, float dynamic_accel_y, float dynamic_accel_z) {
    constexpr float kGravityConstant = 9.81f;
    const float decay_coefficient = std::exp(-(delta_time_seconds_ * 1000.0f / kVelocityDecayHalfLifeMilliseconds) * 0.693147f);
    velocity_x_ = (velocity_x_ + dynamic_accel_x * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_y_ = (velocity_y_ + dynamic_accel_y * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_z_ = (velocity_z_ + dynamic_accel_z * kGravityConstant * delta_time_seconds_) * decay_coefficient;

    return std::sqrt(velocity_x_ * velocity_x_ + velocity_y_ * velocity_y_ + velocity_z_ * velocity_z_);
}

void BozendoMapping2::detectAxialThrustPeaks(const StaffSoundParams& input_parameters, float dynamic_accel_x, float dynamic_accel_y, float dynamic_accel_z, float tip_x, float tip_y, float tip_z, float gyroscope_magnitude) {
    float dynamic_accel_world_x, dynamic_accel_world_y, dynamic_accel_world_z;
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, input_parameters.qx, input_parameters.qy, input_parameters.qz, dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_world_x, dynamic_accel_world_y, dynamic_accel_world_z);

    // dot product(dynamic_accel_world, staff_world) - positive value means thrust along tip direction
    float current_axial_acceleration = dynamic_accel_world_x * tip_x + dynamic_accel_world_y * tip_y + dynamic_accel_world_z * tip_z;
    float current_axial_jerk = (current_axial_acceleration - previous_axial_acceleration_) / (delta_time_seconds_ + 1e-6f);

    if (thrust_cooldown_seconds_ > 0.f) {
        thrust_cooldown_seconds_ -= delta_time_seconds_;
    }

    // Detect zero-crossing of signed jerk depending on the direction of acceleration
    bool is_positive_peak = (current_axial_acceleration > kPeakAxialAccelerationThreshold) && (previous_axial_jerk_ > 0.f && current_axial_jerk <= 0.f);
    bool is_negative_peak = (current_axial_acceleration < -kPeakAxialAccelerationThreshold) && (previous_axial_jerk_ < 0.f && current_axial_jerk >= 0.f);

    // Gate: must have a peak, low rotation (not a spin), and be off cooldown
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

float BozendoMapping2::updateLabanWeight(float velocity_magnitude) {
    float target_weight = juce::jlimit(0.0f, 1.0f, velocity_magnitude * 0.25f);
    if (target_weight > weight_envelope_) {
        weight_envelope_ = MathHelpers::applyOnePoleFilter(weight_envelope_, target_weight, kWeightAttackCoefficient);
    } else {
        weight_envelope_ = MathHelpers::applyOnePoleFilter(weight_envelope_, target_weight, kWeightReleaseCoefficient);
    }
    return weight_envelope_;
}

float BozendoMapping2::updateLabanTime(float gyroscope_magnitude) {
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

float BozendoMapping2::updateLabanSpace(const StaffSoundParams& input_parameters, float gyroscope_magnitude) {
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

void BozendoMapping2::updateLabanFlow(float dynamic_acceleration_magnitude, float& flow_bound, float& flow_free) {
    float jerk_normalized = juce::jlimit(0.0f, 1.0f, std::abs(dynamic_acceleration_magnitude - previous_dynamic_acceleration_magnitude_) / (delta_time_seconds_ + 1e-6f) / 50.0f);
    previous_dynamic_acceleration_magnitude_ = dynamic_acceleration_magnitude;
    flow_bound_envelope_ = MathHelpers::applyOnePoleFilter(flow_bound_envelope_, jerk_normalized, kFlowSmoothingCoefficient);

    flow_bound = juce::jlimit(0.0f, 1.0f, flow_bound_envelope_);
    flow_free  = 1.0f - flow_bound;
}

void BozendoMapping2::updateSpinClassification(float axis_x, float axis_y, float axis_z) {
    smoothed_rotation_axis_x_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_x_, axis_x, kRotationAxisSmoothingCoefficient);
    smoothed_rotation_axis_y_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_y_, axis_y, kRotationAxisSmoothingCoefficient);
    smoothed_rotation_axis_z_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_z_, axis_z, kRotationAxisSmoothingCoefficient);

    float perpendicular_magnitude = std::sqrt(smoothed_rotation_axis_x_ * smoothed_rotation_axis_x_ + smoothed_rotation_axis_y_ * smoothed_rotation_axis_y_);
    float parallel_magnitude = std::abs(smoothed_rotation_axis_z_);

    is_rotation_axis_vertical_ = perpendicular_magnitude > parallel_magnitude;

    if (is_rotation_axis_vertical_) {
        if (perpendicular_magnitude > 1e-3f) {
            if (!is_reference_azimuth_set_) {
                reference_azimuth_x_ = smoothed_rotation_axis_x_ / perpendicular_magnitude;
                reference_azimuth_y_ = smoothed_rotation_axis_y_ / perpendicular_magnitude;
                is_reference_azimuth_set_ = true;
            }
            float dot_product = (smoothed_rotation_axis_x_ / perpendicular_magnitude) * reference_azimuth_x_ + (smoothed_rotation_axis_y_ / perpendicular_magnitude) * reference_azimuth_y_;
            rotation_spin_direction_ = (dot_product >= 0.f) ? 1.f : -1.f;
        }
    } else {
        rotation_spin_direction_ = (smoothed_rotation_axis_z_ >= 0.f) ? 1.f : -1.f;
    }

    if (is_rotation_axis_vertical_ != was_rotation_axis_vertical_ || rotation_spin_direction_ != previous_rotation_spin_direction_) {
        was_rotation_axis_vertical_ = is_rotation_axis_vertical_;
        previous_rotation_spin_direction_ = rotation_spin_direction_;
        debug.print.cyan(
            is_rotation_axis_vertical_ ? "VERTICAL" : "HORIZONTAL",
            rotation_spin_direction_ > 0.f ? "COUNTER_CLOCKWISE_OR_FORWARD" : "CLOCKWISE_OR_BACKWARD"
        );
    }
}

int BozendoMapping2::convertGyroscopeMagnitudeToScaleStep(float gyroscope_magnitude) noexcept {
    float clamped_gyroscope = juce::jlimit(kGyroscopeFloor, kGyroscopeCeiling, gyroscope_magnitude);
    float normalized_gyroscope = (clamped_gyroscope - kGyroscopeFloor) / (kGyroscopeCeiling - kGyroscopeFloor);
    float float_step = normalized_gyroscope * static_cast<float>(kNumberOfScaleSteps - 1);

    float hysteresis_normalized = kScaleHysteresis / (kGyroscopeCeiling - kGyroscopeFloor);
    float hysteresis_steps = hysteresis_normalized * static_cast<float>(kNumberOfScaleSteps - 1);

    int target_step = juce::jlimit(0, kNumberOfScaleSteps - 1, static_cast<int>(float_step + 0.5f));

    if (target_step > current_scale_step_) {
        if (float_step < static_cast<float>(current_scale_step_) + 0.5f + hysteresis_steps) {
            target_step = current_scale_step_;
        }
    } else if (target_step < current_scale_step_) {
        if (float_step > static_cast<float>(current_scale_step_) - 0.5f - hysteresis_steps) {
            target_step = current_scale_step_;
        }
    }
    return target_step;
}

void BozendoMapping2::applyPitchAndChordToOutput(MappingOutput& mapping_output, const StaffSoundParams& input_parameters) {
    // Use staff tilt (pitch) to control the octave.
    int octave_offset = 0;
    if (input_parameters.pitch > 0.5f) {
        octave_offset = 12; // Pointing up
    } else if (input_parameters.pitch < -0.5f) {
        octave_offset = -12; // Pointing down
    }

    float base_semitones = 0.f;

    if (is_rotation_axis_vertical_) {
        if (rotation_spin_direction_ > 0.f) {
            // Vertical Forward: Major 7th arpeggio
            const float scale[kNumberOfScaleSteps] = {0.f, 4.f, 7.f, 11.f};
            base_semitones = scale[current_scale_step_];
            mapping_output.chordSemitones[0] = 4.f; mapping_output.chordSemitones[1] = 7.f;  mapping_output.chordSemitones[2] = 11.f;
        } else {
            // Vertical Backward: Minor 7th arpeggio
            const float scale[kNumberOfScaleSteps] = {0.f, 3.f, 7.f, 10.f};
            base_semitones = scale[current_scale_step_];
            mapping_output.chordSemitones[0] = 3.f; mapping_output.chordSemitones[1] = 7.f;  mapping_output.chordSemitones[2] = 10.f;
        }
    } else {
        if (rotation_spin_direction_ > 0.f) {
            // Horizontal Forward: Sus4 arpeggio
            const float scale[kNumberOfScaleSteps] = {0.f, 5.f, 7.f, 12.f};
            base_semitones = scale[current_scale_step_];
            mapping_output.chordSemitones[0] = 5.f; mapping_output.chordSemitones[1] = 7.f;  mapping_output.chordSemitones[2] = 12.f;
        } else {
            // Horizontal Backward: Diminished arpeggio
            const float scale[kNumberOfScaleSteps] = {0.f, 3.f, 6.f, 9.f};
            base_semitones = scale[current_scale_step_];
            mapping_output.chordSemitones[0] = 3.f; mapping_output.chordSemitones[1] = 6.f;  mapping_output.chordSemitones[2] = 9.f;
        }
    }

    mapping_output.rootHz = MathHelpers::convertSemitonesToHertz(base_semitones + octave_offset, kRootFrequencyHz);
}

void BozendoMapping2::applyVoicesToOutput(MappingOutput& mapping_output, float smoothed_gyroscope, float melody_gain) {
    mapping_output.numVoices = 4;
    mapping_output.voiceGain[0] = melody_gain;

    // Use gyroscope magnitude (bow speed) to fade in the chord voices
    float bow_speed_normalized = juce::jlimit(0.0f, 1.0f, smoothed_gyroscope / 500.0f);

    mapping_output.voiceGain[1] = juce::jlimit(0.0f, 1.0f, (bow_speed_normalized - 0.1f) * 3.0f) * 0.8f;
    mapping_output.voiceGain[2] = juce::jlimit(0.0f, 1.0f, (bow_speed_normalized - 0.3f) * 3.0f) * 0.7f;
    mapping_output.voiceGain[3] = juce::jlimit(0.f, 1.f, (bow_speed_normalized - 0.5f) * 3.0f) * 0.6f;
}

void BozendoMapping2::applyMasterGainToOutput(MappingOutput& mapping_output, float laban_weight, float motion_gate) {
    mapping_output.masterGain = motion_gate * (0.05f + laban_weight * 0.70f);
    // peak punches through the gain gate
    mapping_output.masterGain = juce::jlimit(0.f, 1.f, mapping_output.masterGain + axial_thrust_peak_envelope_ * 0.6f);
}

void BozendoMapping2::applyTimbreToOutput(MappingOutput& mapping_output, float laban_space_focus, float laban_weight) {
    constexpr float kDirectGain[6]   = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
    constexpr float kIndirectGain[6] = { 1.0f, 0.10f, 0.50f, 0.05f, 0.30f, 0.03f };
    for (int i = 0; i < 6; ++i) {
        mapping_output.partialAmps[i] = kDirectGain[i] * laban_space_focus + kIndirectGain[i] * (1.f - laban_space_focus);
    }
    mapping_output.driveAmt = laban_weight * (1.f - laban_space_focus * 0.7f) * 2.5f;

    // peak adds bright transient: boost upper partials
    float peak_brightness = axial_thrust_peak_envelope_ * 0.8f;
    mapping_output.partialAmps[3] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[3] + peak_brightness * 0.5f);
    mapping_output.partialAmps[4] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[4] + peak_brightness * 0.7f);
    mapping_output.partialAmps[5] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[5] + peak_brightness * 0.9f);
    mapping_output.driveAmt = juce::jlimit(0.f, 4.f, mapping_output.driveAmt + axial_thrust_peak_envelope_ * 2.0f);
}

void BozendoMapping2::applyNoiseToOutput(MappingOutput& mapping_output, float suddenness_normalized) {
    if (suddenness_normalized > 0.3f) {
        noise_envelope_ = juce::jlimit(0.0f, 1.0f, noise_envelope_ + (suddenness_normalized - 0.3f) * 1.5f);
    }
    // peak injects its own sharp noise burst
    noise_envelope_ = juce::jlimit(0.f, 1.f, noise_envelope_ + axial_thrust_peak_envelope_ * 0.5f);
    noise_envelope_ *= kNoiseDecayCoefficient;

    mapping_output.noiseAmount = noise_envelope_ * 0.4f;
    mapping_output.noiseLpCoef = 1.f - (0.2f + suddenness_normalized * 0.4f + axial_thrust_peak_envelope_ * 0.4f);
}

void BozendoMapping2::applyModulationToOutput(MappingOutput& mapping_output, float laban_weight, float flow_bound, float flow_free) {
    mapping_output.vibratoDepth  = flow_free * laban_weight * 0.020f;
    mapping_output.vibratoRateHz = 4.5f + smoothed_gyroscope_magnitude_ * 0.005f;
    mapping_output.tremoloDepth  = flow_bound * laban_weight * 0.30f;
    mapping_output.tremoloRateHz = 3.0f + flow_bound * 4.0f;
}

void BozendoMapping2::applyStereoPanToOutput(MappingOutput& mapping_output, float flow_free, float axis_x, float axis_y) {
    float pan_bias = 0.f;
    float perpendicular_magnitude = std::sqrt(axis_x * axis_x + axis_y * axis_y);
    if (perpendicular_magnitude > 1e-3f) {
        float azimuth = std::atan2(axis_y, axis_x);
        pan_bias = juce::jlimit(-0.2f, 0.2f, azimuth / juce::MathConstants<float>::pi * 0.2f);
    }
    const float spread = 0.3f + flow_free * 0.5f;
    mapping_output.panL[0] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f + pan_bias);
    mapping_output.panR[0] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f - pan_bias);
    mapping_output.panL[1] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f + pan_bias);
    mapping_output.panR[1] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f - pan_bias);
    mapping_output.panL[2] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f + pan_bias);
    mapping_output.panR[2] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f - pan_bias);
    mapping_output.panL[3] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f + pan_bias);
    mapping_output.panR[3] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f - pan_bias);
}

void BozendoMapping2::process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) {
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

    float laban_flow_bound, laban_flow_free;
    updateLabanFlow(dynamic_accel_magnitude, laban_flow_bound, laban_flow_free);

    const bool is_moving = smoothed_gyroscope_magnitude_ > kGyroscopeFloor;
    if (is_moving) {
        updateSpinClassification(rotation_axis_x, rotation_axis_y, rotation_axis_z);
        current_scale_step_ = convertGyroscopeMagnitudeToScaleStep(smoothed_gyroscope_magnitude_);
    }

    applyPitchAndChordToOutput(mapping_output, input_parameters);

    float melody_gain = is_moving ? juce::jlimit(0.2f, 1.0f, 0.2f + smoothed_gyroscope_magnitude_ / kGyroscopeCeiling * 0.8f) : 0.0f;
    applyVoicesToOutput(mapping_output, smoothed_gyroscope_magnitude_, melody_gain);

    float motion_gate = juce::jlimit(0.0f, 1.0f, (smoothed_gyroscope_magnitude_ - kGyroscopeFloor * 0.5f) / (kGyroscopeFloor * 1.5f));
    applyMasterGainToOutput(mapping_output, laban_weight, motion_gate);

    applyTimbreToOutput(mapping_output, laban_space_focus, laban_weight);
    applyNoiseToOutput(mapping_output, laban_time_suddenness_normalized);
    applyModulationToOutput(mapping_output, laban_weight, laban_flow_bound, laban_flow_free);
    applyStereoPanToOutput(mapping_output, laban_flow_free, rotation_axis_x, rotation_axis_y);
}