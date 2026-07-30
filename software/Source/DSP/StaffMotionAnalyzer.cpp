#include "StaffMotionAnalyzer.h"
#include "BoStaffSynth.h"
#include "../UI/DebugLog.h"
#include <algorithm>
#include <cmath>

StaffMotionAnalyzer::DerivedMotionFrame StaffMotionAnalyzer::computeFrame(const StaffSoundParams& input_parameters) {
    DerivedMotionFrame frame;

    calculateDeltaTime();
    frame.deltaTimeSeconds = delta_time_seconds_;

    float tip_x, tip_y, tip_z;
    updateTipPositionHistory(input_parameters, tip_x, tip_y, tip_z);
    frame.tipX = tip_x;
    frame.tipY = tip_y;

    calculateRotationAxisAtMidpoint(frame.rotationAxisX, frame.rotationAxisY, frame.rotationAxisZ);

    updateGravityVector(input_parameters);

    float dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_magnitude;
    calculateDynamicAcceleration(input_parameters, dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_magnitude);

    float velocity_magnitude = integrateVelocityForLabanWeight(dynamic_accel_x, dynamic_accel_y, dynamic_accel_z);

    const float gyroscope_magnitude = std::sqrt(input_parameters.gx * input_parameters.gx + input_parameters.gy * input_parameters.gy + input_parameters.gz * input_parameters.gz);
    frame.gyroscopeMagnitude = gyroscope_magnitude;
    frame.smoothedGyroscopeMagnitude = updateGyroscopeMagnitude(gyroscope_magnitude);

    detectAxialThrustPeaks(input_parameters, dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, tip_x, tip_y, tip_z, gyroscope_magnitude);
    frame.axialThrustPeakEnvelope = axialThrustPeakEnvelope();

    frame.labanWeight = updateLabanWeight(velocity_magnitude);
    frame.labanTimeSuddenness = updateLabanTime(gyroscope_magnitude);
    frame.labanSpaceFocus = updateLabanSpace(input_parameters, gyroscope_magnitude);
    updateLabanFlow(dynamic_accel_magnitude, frame.labanFlowBound, frame.labanFlowFree);

    frame.isMoving = frame.smoothedGyroscopeMagnitude > kGyroscopeFloor;

    return frame;
}

void StaffMotionAnalyzer::prepare() {
    delta_time_seconds_ = 1.0f / 100.0f;
    last_timestamp_ticks_ = 0;

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

    reference_azimuth_x_ = 1.f;
    reference_azimuth_y_ = 0.f;
    is_reference_azimuth_set_ = false;

    is_facing_north_ = true;
    previous_is_facing_north_ = true;

    accumulated_spin_degrees_ = 0.f;
    continuous_spin_count_ = 0;

    axial_thrust_peak_envelope_ = 0.f;
    previous_axial_acceleration_ = 0.f;
    previous_axial_jerk_ = 0.f;
    thrust_cooldown_seconds_ = 0.f;
}

void StaffMotionAnalyzer::calculateDeltaTime() {
    const juce::int64 current_time_ticks = juce::Time::getHighResolutionTicks();
    if (last_timestamp_ticks_ != 0) {
        double elapsed_seconds = juce::Time::highResolutionTicksToSeconds(current_time_ticks - last_timestamp_ticks_);
        if (elapsed_seconds > 0.0001 && elapsed_seconds < 0.2) {
            delta_time_seconds_ = static_cast<float>(elapsed_seconds);
        }
    }
    last_timestamp_ticks_ = current_time_ticks;
}

void StaffMotionAnalyzer::updateTipPositionHistory(const StaffSoundParams& input_parameters, float& current_tip_x, float& current_tip_y, float& current_tip_z) {
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, input_parameters.qx, input_parameters.qy, input_parameters.qz, 1.f, 0.f, 0.f, current_tip_x, current_tip_y, current_tip_z);

    // shift ring buffer: [2]=oldest, [1]=mid, [0]=newest
    tip_position_x_history_[2] = tip_position_x_history_[1]; tip_position_x_history_[1] = tip_position_x_history_[0]; tip_position_x_history_[0] = current_tip_x;
    tip_position_y_history_[2] = tip_position_y_history_[1]; tip_position_y_history_[1] = tip_position_y_history_[0]; tip_position_y_history_[0] = current_tip_y;
    tip_position_z_history_[2] = tip_position_z_history_[1]; tip_position_z_history_[1] = tip_position_z_history_[0]; tip_position_z_history_[0] = current_tip_z;
}

void StaffMotionAnalyzer::calculateRotationAxisAtMidpoint(float& axis_x, float& axis_y, float& axis_z) const {
    // velocity_mean = (position[0] - position[2]) / 2 (central difference, 1-frame latency)
    float velocity_mean_x = (tip_position_x_history_[0] - tip_position_x_history_[2]) * 0.5f;
    float velocity_mean_y = (tip_position_y_history_[0] - tip_position_y_history_[2]) * 0.5f;
    float velocity_mean_z = (tip_position_z_history_[0] - tip_position_z_history_[2]) * 0.5f;

    // axis = position[1] cross_product velocity_mean (rotation axis at midpoint)
    axis_x = tip_position_y_history_[1] * velocity_mean_z - tip_position_z_history_[1] * velocity_mean_y;
    axis_y = tip_position_z_history_[1] * velocity_mean_x - tip_position_x_history_[1] * velocity_mean_z;
    axis_z = tip_position_x_history_[1] * velocity_mean_y - tip_position_y_history_[1] * velocity_mean_x;
}

void StaffMotionAnalyzer::updateGravityVector(const StaffSoundParams& input_parameters) {
    float gravity_x_temp, gravity_y_temp, gravity_z_temp;
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, -input_parameters.qx, -input_parameters.qy, -input_parameters.qz, 0.f, 0.f, 1.f, gravity_x_temp, gravity_y_temp, gravity_z_temp);
    constexpr float kGravitySmoothingAlpha = 0.10f; // Time Constant approx 90ms
    gravity_x_ = MathHelpers::applyOnePoleFilter(gravity_x_, gravity_x_temp, kGravitySmoothingAlpha);
    gravity_y_ = MathHelpers::applyOnePoleFilter(gravity_y_, gravity_y_temp, kGravitySmoothingAlpha);
    gravity_z_ = MathHelpers::applyOnePoleFilter(gravity_z_, gravity_z_temp, kGravitySmoothingAlpha);
}

void StaffMotionAnalyzer::calculateDynamicAcceleration(const StaffSoundParams& input_parameters, float& dynamic_accel_x, float& dynamic_accel_y, float& dynamic_accel_z, float& dynamic_accel_magnitude) const {
    dynamic_accel_x = input_parameters.ax - gravity_x_;
    dynamic_accel_y = input_parameters.ay - gravity_y_;
    dynamic_accel_z = input_parameters.az - gravity_z_;
    dynamic_accel_magnitude = std::sqrt(dynamic_accel_x * dynamic_accel_x + dynamic_accel_y * dynamic_accel_y + dynamic_accel_z * dynamic_accel_z);
}

float StaffMotionAnalyzer::integrateVelocityForLabanWeight(float dynamic_accel_x, float dynamic_accel_y, float dynamic_accel_z) {
    constexpr float kGravityConstant = 9.81f;
    const float decay_coefficient = std::exp(-(delta_time_seconds_ * 1000.0f / kVelocityDecayHalfLifeMilliseconds) * 0.693147f);
    velocity_x_ = (velocity_x_ + dynamic_accel_x * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_y_ = (velocity_y_ + dynamic_accel_y * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_z_ = (velocity_z_ + dynamic_accel_z * kGravityConstant * delta_time_seconds_) * decay_coefficient;

    return std::sqrt(velocity_x_ * velocity_x_ + velocity_y_ * velocity_y_ + velocity_z_ * velocity_z_);
}

void StaffMotionAnalyzer::detectAxialThrustPeaks(const StaffSoundParams& input_parameters, float dynamic_accel_x, float dynamic_accel_y, float dynamic_accel_z, float tip_x, float tip_y, float tip_z, float gyroscope_magnitude) {
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

float StaffMotionAnalyzer::updateGyroscopeMagnitude(float gyroscope_magnitude) {
    smoothed_gyroscope_magnitude_ = MathHelpers::applyOnePoleFilter(smoothed_gyroscope_magnitude_, gyroscope_magnitude, kGyroscopeSmoothingCoefficient);
    return smoothed_gyroscope_magnitude_;
}

float StaffMotionAnalyzer::updateLabanWeight(float velocity_magnitude) {
    float target_weight = juce::jlimit(0.0f, 1.0f, velocity_magnitude * 0.25f);
    if (target_weight > weight_envelope_) {
        weight_envelope_ = MathHelpers::applyOnePoleFilter(weight_envelope_, target_weight, kWeightAttackCoefficient);
    } else {
        weight_envelope_ = MathHelpers::applyOnePoleFilter(weight_envelope_, target_weight, kWeightReleaseCoefficient);
    }
    return weight_envelope_;
}

float StaffMotionAnalyzer::updateLabanTime(float gyroscope_magnitude) {
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

float StaffMotionAnalyzer::updateLabanSpace(const StaffSoundParams& input_parameters, float gyroscope_magnitude) {
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

void StaffMotionAnalyzer::updateLabanFlow(float dynamic_acceleration_magnitude, float& flow_bound, float& flow_free) {
    float jerk_normalized = juce::jlimit(0.0f, 1.0f, std::abs(dynamic_acceleration_magnitude - previous_dynamic_acceleration_magnitude_) / (delta_time_seconds_ + 1e-6f) / 50.0f);
    previous_dynamic_acceleration_magnitude_ = dynamic_acceleration_magnitude;
    flow_bound_envelope_ = MathHelpers::applyOnePoleFilter(flow_bound_envelope_, jerk_normalized, kFlowSmoothingCoefficient);

    flow_bound = juce::jlimit(0.0f, 1.0f, flow_bound_envelope_);
    flow_free  = 1.0f - flow_bound;
}

bool StaffMotionAnalyzer::updateSpinClassificationByAbsoluteComponent(float axis_x, float axis_y, float axis_z) {
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

bool StaffMotionAnalyzer::updateSpinClassificationByReferenceAzimuth(float axis_x, float axis_y, float axis_z) {
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

    bool spin_changed = false;
    constexpr float kEps = 1e-6f;
    if (is_rotation_axis_vertical_ != was_rotation_axis_vertical_ || std::abs(rotation_spin_direction_ - previous_rotation_spin_direction_) > kEps) {
        was_rotation_axis_vertical_ = is_rotation_axis_vertical_;
        previous_rotation_spin_direction_ = rotation_spin_direction_;
        spin_changed = true;
    }
    return spin_changed;
}

bool StaffMotionAnalyzer::updateFacingClassification(float tip_x, float tip_y) {
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

int StaffMotionAnalyzer::accumulateContinuousSpins(bool spin_classification_changed, float gyroscope_magnitude) {
    if (spin_classification_changed) {
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
    return continuous_spin_count_;
}
