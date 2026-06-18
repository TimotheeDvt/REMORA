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

    smoothed_rotation_azimuth_rad_ = 0.f;
    current_azimuth_sector_        = 0;
    previous_azimuth_sector_       = -1;

    is_rotation_axis_vertical_ = false;
    rotation_spin_direction_  = 1.f;
    reference_azimuth_x_   = 1.f;
    reference_azimuth_y_   = 0.f;
    is_reference_azimuth_set_ = false;
    was_rotation_axis_vertical_ = false;
    previous_rotation_spin_direction_ = 1.f;

    accumulated_spin_degrees_ = 0.f;
    continuous_spin_count_ = 0;

    smoothed_lpf_cutoff_hz_ = 20000.f;

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

        // debug.print.magenta("THRUST peak | axial:", current_axial_acceleration_absolute, "gyroscope:", gyroscope_magnitude);
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

void BozendoMapping2::updateRotationAzimuth(float axis_x, float axis_y) {
    float perp_mag = std::sqrt(axis_x * axis_x + axis_y * axis_y);
    if (perp_mag < 1e-3f) return; // axis too vertical to have meaningful azimuth

    // Raw azimuth of the smoothed rotation axis in the world XY plane
    float raw_azimuth = std::atan2(smoothed_rotation_axis_y_, smoothed_rotation_axis_x_);

    // Unwrap and smooth using a circular mean trick to avoid atan2 discontinuity
    float delta = raw_azimuth - smoothed_rotation_azimuth_rad_;
    // Wrap delta into [-pi, pi]
    constexpr float pi = 3.14159265f;
    while (delta >  pi) delta -= 2.f * pi;
    while (delta < -pi) delta += 2.f * pi;

    smoothed_rotation_azimuth_rad_ += kAzimuthSmoothingCoefficient * delta;

    // Wrap result back into [-pi, pi]
    while (smoothed_rotation_azimuth_rad_ >  pi) smoothed_rotation_azimuth_rad_ -= 2.f * pi;
    while (smoothed_rotation_azimuth_rad_ < -pi) smoothed_rotation_azimuth_rad_ += 2.f * pi;

    current_azimuth_sector_ = computeAzimuthSector(smoothed_rotation_azimuth_rad_);

    if (current_azimuth_sector_ != previous_azimuth_sector_) {
        previous_azimuth_sector_ = current_azimuth_sector_;
        // debug.print.yellow("Azimuth sector changed:", current_azimuth_sector_,
        //                    "| angle:", smoothed_rotation_azimuth_rad_);
    }
}

int BozendoMapping2::computeAzimuthSector(float azimuth_rad) const noexcept {
    constexpr float pi = 3.14159265f;
    constexpr float quarterPi = pi / 4.f;
    int target_sector = static_cast<int>(std::floor((azimuth_rad + pi) / (pi * 0.5f))) % 4;
    target_sector = juce::jlimit(0, 3, target_sector);
    if (target_sector == current_azimuth_sector_) return current_azimuth_sector_;
    float sector_center = -pi + target_sector * (pi * 0.5f) + quarterPi;
    float dist_to_center = std::abs(azimuth_rad - sector_center);
    while (dist_to_center > pi) dist_to_center -= pi;
    float sector_half_width = pi * 0.25f - kAzimuthSectorHysteresisRad;
    if (dist_to_center < sector_half_width) return target_sector;
    return current_azimuth_sector_; // stay in current sector
}

bool BozendoMapping2::updateSpinClassification(float axis_x, float axis_y, float axis_z) {
    smoothed_rotation_axis_x_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_x_, axis_x, kRotationAxisSmoothingCoefficient);
    smoothed_rotation_axis_y_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_y_, axis_y, kRotationAxisSmoothingCoefficient);
    smoothed_rotation_axis_z_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_z_, axis_z, kRotationAxisSmoothingCoefficient);

    float perpendicular_magnitude = std::sqrt(smoothed_rotation_axis_x_ * smoothed_rotation_axis_x_ + smoothed_rotation_axis_y_ * smoothed_rotation_axis_y_);
    float parallel_magnitude = std::abs(smoothed_rotation_axis_z_);

    is_rotation_axis_vertical_ = perpendicular_magnitude > parallel_magnitude;

    if (is_rotation_axis_vertical_) {
        updateRotationAzimuth(smoothed_rotation_axis_x_, smoothed_rotation_axis_y_);
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

    bool spin_changed = false;
    if (is_rotation_axis_vertical_ != was_rotation_axis_vertical_ || rotation_spin_direction_ != previous_rotation_spin_direction_) {
        was_rotation_axis_vertical_ = is_rotation_axis_vertical_;
        previous_rotation_spin_direction_ = rotation_spin_direction_;
        spin_changed = true;

        if (!is_rotation_axis_vertical_) {
            previous_azimuth_sector_ = -1;
        }
    }
    return spin_changed;
}

void BozendoMapping2::applyPitchAndChordToOutput(MappingOutput& mapping_output, const StaffSoundParams& input_parameters) {
    juce::ignoreUnused(input_parameters);

    if (is_rotation_axis_vertical_) {
        // Azimuth sector (0-3) selects root note from a 4-note set
        // CW/CCW selects chord quality offset
        constexpr float kAzimuthNotes[4] = {
            0.f,   // Sector 0 (~East/West axis): C
            7.f,   // Sector 1 (~North/South axis): G
            4.f,   // Sector 2 (~West/East axis): E
            9.f,   // Sector 3 (~South/North axis): A
        };
        float base = kAzimuthNotes[current_azimuth_sector_];
        // CW: add a minor third offset (darker quality)
        // CCW: stay on the base note (brighter quality)
        target_base_semitones_ = base + (rotation_spin_direction_ > 0.f ? 0.f : 3.f);
    } else {
        if (rotation_spin_direction_ < 0.f) {
            // CW Horizontal: 3rd Note (G)
            target_base_semitones_ = 7.f;
        } else {
            // CCW Horizontal: 4th Note (A)
            target_base_semitones_ = 9.f;
        }
    }

    // Morph speed scales with how fast the staff is moving
    float morph_speed = juce::jlimit(0.005f, 0.2f, smoothed_gyroscope_magnitude_ / 2000.0f);
    current_base_semitones_ = MathHelpers::applyOnePoleFilter(current_base_semitones_, target_base_semitones_, morph_speed);

    // Instead of a chord, we only play octaves of the root note to maintain the same pitch class.
    mapping_output.chordSemitones[0] = 12.f;  // +1 Octave
    mapping_output.chordSemitones[1] = 7.f;  // +5th
    mapping_output.chordSemitones[2] = -12.f; // -1 Octave

    mapping_output.rootHz = MathHelpers::convertSemitonesToHertz(current_base_semitones_, kRootFrequencyHz);
}

void BozendoMapping2::applyVoicesToOutput(MappingOutput& mapping_output, float melody_gain) {
    mapping_output.numVoices = 4;
    mapping_output.voiceGain[0] = melody_gain;

    // All voices play together, with gains relative to the melody gain.
    mapping_output.voiceGain[1] = melody_gain * 0.8f;
    mapping_output.voiceGain[2] = melody_gain * 0.7f;
    mapping_output.voiceGain[3] = melody_gain * 0.6f;
}

void BozendoMapping2::applyMasterGainToOutput(MappingOutput& mapping_output, float laban_weight, float motion_gate) {
    mapping_output.masterGain = motion_gate * (0.05f + laban_weight * 0.70f);
    // peak punches through the gain gate
    mapping_output.masterGain = juce::jlimit(0.f, 1.f, mapping_output.masterGain + axial_thrust_peak_envelope_ * 0.6f);
}

void BozendoMapping2::applyTimbreToOutput(MappingOutput& mapping_output, float laban_space_focus, float laban_weight) {
    juce::ignoreUnused(laban_space_focus);
    constexpr float kConstantGain[6] = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
    for (int i = 0; i < 6; ++i) {
        mapping_output.partialAmps[i] = kConstantGain[i];
    }
    mapping_output.driveAmt = 1.0f + laban_weight * 1.0f;

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
    juce::ignoreUnused(laban_weight, flow_bound, flow_free);
    mapping_output.vibratoDepth  = 0.0f;
    mapping_output.vibratoRateHz = 0.0f;
    mapping_output.tremoloDepth  = 0.0f;
    mapping_output.tremoloRateHz = 0.0f;
}

void BozendoMapping2::applyStereoPanToOutput(MappingOutput& mapping_output, float flow_free, float axis_x, float axis_y) {
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
    bool spin_changed = false;
    if (is_moving) {
        spin_changed = updateSpinClassification(rotation_axis_x, rotation_axis_y, rotation_axis_z);

        if (spin_changed) {
            accumulated_spin_degrees_ = 0.f;
            continuous_spin_count_ = 0;
        } else {
            accumulated_spin_degrees_ += gyroscope_magnitude * delta_time_seconds_;
            while (accumulated_spin_degrees_ >= 360.0f) {
                accumulated_spin_degrees_ -= 360.0f;
                continuous_spin_count_++;
                // debug.print.magenta("Full circles in current spin:", continuous_spin_count_);
            }
        }
    }

    applyPitchAndChordToOutput(mapping_output, input_parameters);

    if (spin_changed) {
        if (is_rotation_axis_vertical_) {
            constexpr const char* kSectorNames[4] = { "EW", "NT", "WE", "SN" };
            debug.print.cyan("V",
                rotation_spin_direction_ > 0.f ? "CCW" : "CW",
                "| Facing:", kSectorNames[current_azimuth_sector_],
                "| Azimuth:", smoothed_rotation_azimuth_rad_);
        } else {
            const char* note_name = (rotation_spin_direction_ < 0.f) ? "G" : "A";
            debug.print.cyan(
                "H",
                rotation_spin_direction_ > 0.f ? "CCW" : "CW",
                "| :", note_name
            );
        }
    }

    float melody_gain = is_moving ? 1.0f : 0.0f;
    applyVoicesToOutput(mapping_output, melody_gain);

    float motion_gate = juce::jlimit(0.0f, 1.0f, (smoothed_gyroscope_magnitude_ - kGyroscopeFloor * 0.5f) / (kGyroscopeFloor * 1.5f));
    applyMasterGainToOutput(mapping_output, laban_weight, motion_gate);

    applyTimbreToOutput(mapping_output, laban_space_focus, laban_weight);
    applyNoiseToOutput(mapping_output, laban_time_suddenness_normalized);
    applyModulationToOutput(mapping_output, laban_weight, laban_flow_bound, laban_flow_free);
    applyStereoPanToOutput(mapping_output, laban_flow_free, rotation_axis_x, rotation_axis_y);

    // Morph global LPF cutoff using a sine wave driven by the spin count
    // Increased multiplier to 1.5f so it sweeps back and forth faster
    float spin_phase = static_cast<float>(continuous_spin_count_) * 1.5f;
    float sine_val = std::sin(spin_phase);

    // Map sine wave output (-1.0 to 1.0) to a frequency range (400 Hz to 20000 Hz)
    float target_lpf_cutoff = juce::jmap(sine_val, -1.0f, 1.0f, 400.0f, 20000.0f);
    // Lowered smoothing coefficient to 0.03f for a more fluid glide
    smoothed_lpf_cutoff_hz_ = MathHelpers::applyOnePoleFilter(smoothed_lpf_cutoff_hz_, target_lpf_cutoff, 0.03f);

    mapping_output.lpfCutoffHz = smoothed_lpf_cutoff_hz_;
}