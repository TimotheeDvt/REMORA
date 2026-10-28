#include "StaffMotionAnalyzer.h"
#include "StaffSoundParams.h"
#include "../UI/DebugLog.h"
#include <algorithm>
#include <cmath>

StaffMotionAnalyzer::DerivedMotionFrame StaffMotionAnalyzer::computeFrame(const StaffSoundParams& input_parameters) {
    DerivedMotionFrame frame;

    delta_time_seconds_ = input_parameters.deltaTimeSeconds > 0.f ? input_parameters.deltaTimeSeconds : 0.01f;
    ticks_ = referenceTicks(delta_time_seconds_);
    frame.deltaTimeSeconds = delta_time_seconds_;

    // Tip direction: the staff's +X in the calibrated world frame.
    MathHelpers::rotateVectorByQuaternion(input_parameters.qw, input_parameters.qx, input_parameters.qy, input_parameters.qz,
                                          1.f, 0.f, 0.f, tip_x_, tip_y_, tip_z_);
    frame.tipX = tip_x_;
    frame.tipY = tip_y_;
    frame.tipZ = tip_z_;

    // Rotation axis straight from the gyroscope (already in the world frame), minus the roll about
    // the staff itself: w_perp = w - tip (tip . w). This is what the tip's motion traces out, without
    // differentiating orientation.
    const float wx = input_parameters.worldGyroX, wy = input_parameters.worldGyroY, wz = input_parameters.worldGyroZ;
    const float along = wx * tip_x_ + wy * tip_y_ + wz * tip_z_;
    const float px = wx - along * tip_x_, py = wy - along * tip_y_, pz = wz - along * tip_z_;
    frame.spinRateDegPerSec = std::sqrt(px * px + py * py + pz * pz);
    frame.rotationAxisX = px / kGyroscopeCeiling;
    frame.rotationAxisY = py / kGyroscopeCeiling;
    frame.rotationAxisZ = pz / kGyroscopeCeiling;
    world_gyro_z_ = wz;
    frame.worldGyroZ = wz;

    const float forward_alpha = coefficientFor(kForwardAxisSmoothingCoefficient, ticks_);
    smoothed_forward_axis_x_ = MathHelpers::applyOnePoleFilter(smoothed_forward_axis_x_, tip_x_, forward_alpha);
    smoothed_forward_axis_y_ = MathHelpers::applyOnePoleFilter(smoothed_forward_axis_y_, tip_y_, forward_alpha);

    // Dynamic acceleration: specific force and gravity are both in the world frame, so this is a
    // plain subtraction. During spins it is dominated by centripetal acceleration (w^2 r).
    const float dynamic_accel_x = input_parameters.worldAccelX;
    const float dynamic_accel_y = input_parameters.worldAccelY;
    const float dynamic_accel_z = input_parameters.worldAccelZ - 1.0f;
    const float dynamic_accel_magnitude = std::sqrt(dynamic_accel_x * dynamic_accel_x + dynamic_accel_y * dynamic_accel_y + dynamic_accel_z * dynamic_accel_z);

    constexpr float kGravityConstant = 9.81f;
    const float decay_coefficient = std::exp(-(delta_time_seconds_ * 1000.0f / kVelocityDecayHalfLifeMilliseconds) * 0.693147f);
    velocity_x_ = (velocity_x_ + dynamic_accel_x * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_y_ = (velocity_y_ + dynamic_accel_y * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    velocity_z_ = (velocity_z_ + dynamic_accel_z * kGravityConstant * delta_time_seconds_) * decay_coefficient;
    const float velocity_magnitude = std::sqrt(velocity_x_ * velocity_x_ + velocity_y_ * velocity_y_ + velocity_z_ * velocity_z_);

    const float gyroscope_magnitude = std::sqrt(input_parameters.gx * input_parameters.gx + input_parameters.gy * input_parameters.gy + input_parameters.gz * input_parameters.gz);
    frame.gyroscopeMagnitude = gyroscope_magnitude;
    smoothed_gyroscope_magnitude_ = MathHelpers::applyOnePoleFilter(smoothed_gyroscope_magnitude_, gyroscope_magnitude,
                                                                    coefficientFor(kGyroscopeSmoothingCoefficient, ticks_));
    frame.smoothedGyroscopeMagnitude = smoothed_gyroscope_magnitude_;

    const float axial_acceleration = dynamic_accel_x * tip_x_ + dynamic_accel_y * tip_y_ + dynamic_accel_z * tip_z_;
    detectAxialThrustPeaks(axial_acceleration, gyroscope_magnitude);
    frame.axialThrustPeakEnvelope = axial_thrust_peak_envelope_;

    frame.labanWeight = updateLabanWeight(velocity_magnitude);
    frame.labanTimeSuddenness = updateLabanTime(gyroscope_magnitude);
    frame.labanSpaceFocus = updateLabanSpace(input_parameters, gyroscope_magnitude);
    updateLabanFlow(dynamic_accel_magnitude, frame.labanFlowBound, frame.labanFlowFree);

    frame.isMoving = frame.smoothedGyroscopeMagnitude > kGyroscopeFloor;

    return frame;
}

void StaffMotionAnalyzer::prepare() {
    *this = StaffMotionAnalyzer();
}

void StaffMotionAnalyzer::detectAxialThrustPeaks(float current_axial_acceleration, float gyroscope_magnitude) {
    // Signed jerk along the staff; positive acceleration means toward the tip.
    const float current_axial_jerk = has_previous_axial_
        ? (current_axial_acceleration - previous_axial_acceleration_) / delta_time_seconds_
        : 0.f;

    if (thrust_cooldown_seconds_ > 0.f) {
        thrust_cooldown_seconds_ -= delta_time_seconds_;
    }

    // Peak = zero-crossing of the signed jerk while the acceleration is beyond threshold.
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
    has_previous_axial_ = true;
    axial_thrust_peak_envelope_ *= decayFor(kPeakDecayCoefficient, ticks_);
}

float StaffMotionAnalyzer::updateLabanWeight(float velocity_magnitude) {
    float target_weight = juce::jlimit(0.0f, 1.0f, velocity_magnitude * 0.25f);
    const float coefficient = target_weight > weight_envelope_ ? kWeightAttackCoefficient : kWeightReleaseCoefficient;
    weight_envelope_ = MathHelpers::applyOnePoleFilter(weight_envelope_, target_weight, coefficientFor(coefficient, ticks_));
    return weight_envelope_;
}

float StaffMotionAnalyzer::updateLabanTime(float gyroscope_magnitude) {
    const float suddenness = has_previous_gyroscope_magnitude_
        ? (gyroscope_magnitude - previous_gyroscope_magnitude_) / delta_time_seconds_
        : 0.f;
    previous_gyroscope_magnitude_ = gyroscope_magnitude;
    has_previous_gyroscope_magnitude_ = true;
    float positive_suddenness = juce::jlimit(0.0f, 1.0f, std::max(0.0f, suddenness) / 800.0f);
    const float coefficient = positive_suddenness > suddenness_envelope_ ? kSuddennessAttackCoefficient : kSuddennessSmoothingCoefficient;
    suddenness_envelope_ = MathHelpers::applyOnePoleFilter(suddenness_envelope_, positive_suddenness, coefficientFor(coefficient, ticks_));
    return juce::jlimit(0.0f, 1.0f, suddenness_envelope_);
}

float StaffMotionAnalyzer::updateLabanSpace(const StaffSoundParams& input_parameters, float gyroscope_magnitude) {
    if (gyroscope_magnitude > 5.0f) {
        float normal_x = input_parameters.gx;
        float normal_y = input_parameters.gy;
        float normal_z = input_parameters.gz;
        MathHelpers::normalize3DVector(normal_x, normal_y, normal_z);
        float dot_product = std::abs(normal_x * previous_gyroscope_direction_x_ + normal_y * previous_gyroscope_direction_y_ + normal_z * previous_gyroscope_direction_z_);
        axis_focus_ = MathHelpers::applyOnePoleFilter(axis_focus_, dot_product, coefficientFor(kAxisFocusSmoothingCoefficient, ticks_));
        previous_gyroscope_direction_x_ = normal_x;
        previous_gyroscope_direction_y_ = normal_y;
        previous_gyroscope_direction_z_ = normal_z;
    } else {
        axis_focus_ = MathHelpers::applyOnePoleFilter(axis_focus_, 0.5f, coefficientFor(kAxisFocusRelaxCoefficient, ticks_));
    }
    return juce::jlimit(0.0f, 1.0f, axis_focus_);
}

void StaffMotionAnalyzer::updateLabanFlow(float dynamic_acceleration_magnitude, float& flow_bound, float& flow_free) {
    const float jerk = has_previous_dynamic_acceleration_
        ? std::abs(dynamic_acceleration_magnitude - previous_dynamic_acceleration_magnitude_) / delta_time_seconds_
        : 0.f;
    float jerk_normalized = juce::jlimit(0.0f, 1.0f, jerk / 50.0f);
    previous_dynamic_acceleration_magnitude_ = dynamic_acceleration_magnitude;
    has_previous_dynamic_acceleration_ = true;
    flow_bound_envelope_ = MathHelpers::applyOnePoleFilter(flow_bound_envelope_, jerk_normalized, coefficientFor(kFlowSmoothingCoefficient, ticks_));

    flow_bound = juce::jlimit(0.0f, 1.0f, flow_bound_envelope_);
    flow_free  = 1.0f - flow_bound;
}

bool StaffMotionAnalyzer::finishSpinClassification() {
    bool spin_changed = false;
    constexpr float kEps = 1e-6f;
    if (is_rotation_axis_vertical_ != was_rotation_axis_vertical_ || std::abs(rotation_spin_direction_ - previous_rotation_spin_direction_) > kEps) {
        was_rotation_axis_vertical_ = is_rotation_axis_vertical_;
        previous_rotation_spin_direction_ = rotation_spin_direction_;
        spin_changed = true;
    }
    return spin_changed;
}

bool StaffMotionAnalyzer::updateSpinClassificationByAbsoluteComponent(float axis_x, float axis_y, float axis_z) {
    const float alpha = coefficientFor(kRotationAxisSmoothingCoefficient, ticks_);
    smoothed_rotation_axis_x_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_x_, axis_x, alpha);
    smoothed_rotation_axis_y_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_y_, axis_y, alpha);
    smoothed_rotation_axis_z_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_z_, axis_z, alpha);

    float perpendicular_magnitude = std::sqrt(smoothed_rotation_axis_x_ * smoothed_rotation_axis_x_ + smoothed_rotation_axis_y_ * smoothed_rotation_axis_y_);
    float parallel_magnitude = std::abs(smoothed_rotation_axis_z_);

    // "Vertical" = the staff turns in a vertical plane, i.e. the rotation axis is mostly horizontal.
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

    return finishSpinClassification();
}

bool StaffMotionAnalyzer::updateSpinClassificationByReferenceAzimuth(float axis_x, float axis_y, float axis_z) {
    const float alpha = coefficientFor(kRotationAxisSmoothingCoefficient, ticks_);
    smoothed_rotation_axis_x_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_x_, axis_x, alpha);
    smoothed_rotation_axis_y_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_y_, axis_y, alpha);
    smoothed_rotation_axis_z_ = MathHelpers::applyOnePoleFilter(smoothed_rotation_axis_z_, axis_z, alpha);

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

    return finishSpinClassification();
}

bool StaffMotionAnalyzer::updateFacingClassification(bool is_moving) {
    // (d_x, d_y) is the normal of the line the staff works along. In a vertical-plane spin that is
    // the horizontal rotation axis; when the staff points somewhere, it is the tip direction turned
    // by 90deg (swapping x and y), so "pointing along x" and "spinning in a plane that contains x"
    // give the same state.
    float facing_x, facing_y;
    const float tip_horizontal = std::sqrt(tip_x_ * tip_x_ + tip_y_ * tip_y_);
    if (is_moving && is_rotation_axis_vertical_) {
        facing_x = smoothed_rotation_axis_x_;
        facing_y = smoothed_rotation_axis_y_;
    } else if (std::abs(world_gyro_z_) < kFacingMaxVerticalRateDegPerSec && tip_horizontal > kFacingMinHorizontalTip) {
        facing_x = smoothed_forward_axis_y_;
        facing_y = smoothed_forward_axis_x_;
    } else {
        return false; // turning about the vertical (overhead spin, body turn) or staff near vertical: hold
    }

    const float abs_x = std::abs(facing_x);
    const float abs_y = std::abs(facing_y);
    if (abs_x + abs_y < 1e-6f)
        return false;

    const bool previous = is_facing_north_;
    if (is_facing_north_) {
        is_facing_north_ = !(abs_y > abs_x * kFacingHysteresisRatio);
    } else {
        is_facing_north_ = (abs_x > abs_y * kFacingHysteresisRatio);
    }
    return is_facing_north_ != previous;
}

int StaffMotionAnalyzer::accumulateContinuousSpins(bool spin_classification_changed, float spin_rate_deg_per_sec) {
    if (spin_classification_changed) {
        accumulated_spin_degrees_ = 0.f;
        continuous_spin_count_ = 0;
    } else {
        accumulated_spin_degrees_ += spin_rate_deg_per_sec * delta_time_seconds_;
        while (accumulated_spin_degrees_ >= 360.0f) {
            accumulated_spin_degrees_ -= 360.0f;
            continuous_spin_count_++;
            debug.print.magenta("Full circles in current spin:", continuous_spin_count_);
        }
    }
    return continuous_spin_count_;
}
