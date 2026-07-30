#pragma once

#include "MathHelpers.h"
#include <JuceHeader.h>

struct StaffSoundParams;

// Shared body-motion analysis engine used by every staff mapping. Owns the per-block gesture
// state - delta time, tip-position/rotation-axis tracking, gravity removal,
// Laban weight/time/space/flow, and axial thrust-peak detection. Each mapping composes one of
// these and keeps only the logic that actually differs between them: how
// spin/facing/thrust feed pitch, timbre, gain and modulation.
class StaffMotionAnalyzer {
public:
    static constexpr float kGyroscopeFloor = 30.0f;
    static constexpr float kGyroscopeCeiling = 750.0f;

    // Which of the two mutually-exclusive spin-classification conventions to
    // use (see updateSpinClassificationByAbsoluteComponent/ByReferenceAzimuth
    // above) - both write the same shared state, so only one may run per block.
    enum class SpinConvention { ByAbsoluteComponent, ByReferenceAzimuth };

    // One-shot-per-block snapshot of every derived signal that doesn't depend
    // on a per-block choice (spin convention) or on this block's own
    // change-detection result (continuous spin count, facing). Those two
    // remain the caller's responsibility - see the Graph "Spin Classification"
    // node, which is the only place that still calls
    // updateSpinClassificationBy*/updateFacingClassification/
    // accumulateContinuousSpins directly, using tipX/tipY/rotationAxis* below.
    struct DerivedMotionFrame {
        float deltaTimeSeconds = 1.0f / 100.0f;
        float gyroscopeMagnitude = 0.f;
        float smoothedGyroscopeMagnitude = 0.f;
        float labanWeight = 0.f;
        float labanTimeSuddenness = 0.f;
        float labanSpaceFocus = 0.f;
        float labanFlowBound = 0.f;
        float labanFlowFree = 0.f;
        bool  isMoving = false;
        float axialThrustPeakEnvelope = 0.f;
        float rotationAxisX = 0.f, rotationAxisY = 0.f, rotationAxisZ = 1.f;
        float tipX = 1.f, tipY = 0.f;
    };

    // Runs the imperative analysis pipeline every mapping used to duplicate
    // (delta-time -> tip position -> rotation axis -> gravity -> dynamic
    // accel -> velocity/Laban weight -> gyro magnitude -> thrust detection ->
    // Laban weight/time/space/flow -> moving gate), once, as the single
    // source of truth. Deliberately stops short of spin classification/
    // facing/continuous-spin-count - see DerivedMotionFrame comment above.
    DerivedMotionFrame computeFrame(const StaffSoundParams& input_parameters);

    void prepare();

    void calculateDeltaTime();
    float deltaTimeSeconds() const noexcept { return delta_time_seconds_; }

    // Rotates the staff's long axis (+X in sensor frame) into world frame and
    // pushes it into the 3-frame ring buffer used for the rotation-axis
    // estimate below. Also doubles as the staff's "facing" vector when the
    // spin plane is horizontal.
    void updateTipPositionHistory(const StaffSoundParams &input_parameters,
                                   float &current_tip_x, float &current_tip_y,
                                   float &current_tip_z);
    void calculateRotationAxisAtMidpoint(float &axis_x, float &axis_y,
                                          float &axis_z) const;

    void updateGravityVector(const StaffSoundParams &input_parameters);
    void calculateDynamicAcceleration(const StaffSoundParams &input_parameters,
                                       float &dynamic_accel_x,
                                       float &dynamic_accel_y,
                                       float &dynamic_accel_z,
                                       float &dynamic_accel_magnitude) const;
    float integrateVelocityForLabanWeight(float dynamic_accel_x,
                                           float dynamic_accel_y,
                                           float dynamic_accel_z);

    void detectAxialThrustPeaks(const StaffSoundParams &input_parameters,
                                 float dynamic_accel_x, float dynamic_accel_y,
                                 float dynamic_accel_z, float tip_x,
                                 float tip_y, float tip_z,
                                 float gyroscope_magnitude);
    float axialThrustPeakEnvelope() const noexcept {
        return axial_thrust_peak_envelope_;
    }

    float updateGyroscopeMagnitude(float gyroscope_magnitude);
    float smoothedGyroscopeMagnitude() const noexcept {
        return smoothed_gyroscope_magnitude_;
    }

    float updateLabanWeight(float velocity_magnitude);
    float updateLabanTime(float gyroscope_magnitude);
    float updateLabanSpace(const StaffSoundParams &input_parameters,
                            float gyroscope_magnitude);
    void updateLabanFlow(float dynamic_acceleration_magnitude,
                          float &flow_bound, float &flow_free);

    // Spin classification: is the rotation axis closer to vertical (world Z)
    // or horizontal, and which way is it spinning. Two interchangeable
    // direction conventions exist across the mapping family:
    //  - ByAbsoluteComponent: direction = sign of whichever horizontal axis
    //    component currently dominates (Azimut / Azimut+ / Azimut Reverb).
    //  - ByReferenceAzimuth: direction = sign of the dot product against the
    //    horizontal azimuth captured the first time a vertical spin is seen
    //    (Bozendo / Bozendo 2).
    // Both return true on the block where the plane or direction changes.
    bool updateSpinClassificationByAbsoluteComponent(float axis_x,
                                                       float axis_y,
                                                       float axis_z);
    bool updateSpinClassificationByReferenceAzimuth(float axis_x, float axis_y,
                                                      float axis_z);

    bool isRotationAxisVertical() const noexcept {
        return is_rotation_axis_vertical_;
    }
    float rotationSpinDirection() const noexcept {
        return rotation_spin_direction_;
    }
    float smoothedRotationAxisX() const noexcept {
        return smoothed_rotation_axis_x_;
    }
    float smoothedRotationAxisY() const noexcept {
        return smoothed_rotation_axis_y_;
    }

    // Facing (Azimut family): which horizontal half-plane the staff's long
    // axis points into, relative to the calibration gesture's "forward"
    // pose. Uses whichever smoothed axis is relevant to the current spin
    // plane, with hysteresis to avoid chatter at the boundary. Returns true
    // on the block where the facing changes.
    bool updateFacingClassification(float tip_x, float tip_y);
    bool isFacingNorth() const noexcept { return is_facing_north_; }

    // Counts full 360-degree rotations since the current spin plane/
    // direction was entered; resets whenever spin classification changes.
    // Used by mappings that sweep a parameter with continuous spinning.
    int accumulateContinuousSpins(bool spin_classification_changed,
                                   float gyroscope_magnitude);
    int continuousSpinCount() const noexcept { return continuous_spin_count_; }

private:
    float delta_time_seconds_ = 1.0f / 100.0f;
    juce::int64 last_timestamp_ticks_ = 0;

    float gravity_x_ = 0.f;
    float gravity_y_ = 0.f;
    float gravity_z_ = 1.f;

    float velocity_x_ = 0.f;
    float velocity_y_ = 0.f;
    float velocity_z_ = 0.f;
    static constexpr float kVelocityDecayHalfLifeMilliseconds = 80.0f;

    float weight_envelope_ = 0.f;
    // Time Constant approx 22ms attack, 1700ms release
    static constexpr float kWeightAttackCoefficient = 0.40f;
    static constexpr float kWeightReleaseCoefficient = 0.006f;

    float previous_gyroscope_magnitude_ = 0.f;
    float suddenness_ = 0.f;
    float suddenness_envelope_ = 0.f;
    static constexpr float kSuddennessSmoothingCoefficient = 0.15f;

    float previous_gyroscope_direction_x_ = 0.f;
    float previous_gyroscope_direction_y_ = 0.f;
    float previous_gyroscope_direction_z_ = 1.f;
    float axis_focus_ = 1.f;
    static constexpr float kAxisFocusSmoothingCoefficient = 0.10f;

    float previous_dynamic_acceleration_magnitude_ = 0.f;
    float flow_bound_envelope_ = 0.f;
    static constexpr float kFlowSmoothingCoefficient = 0.12f;

    float smoothed_gyroscope_magnitude_ = 0.f;
    // Time Constant approx 25ms at 100Hz
    static constexpr float kGyroscopeSmoothingCoefficient = 0.35f;

    // Tip position history - 3 frames, ring buffer
    // tip_position_i = rotate({1,0,0}, quaternion_i) in world frame
    float tip_position_x_history_[3] = {1.f, 1.f, 1.f};
    float tip_position_y_history_[3] = {0.f, 0.f, 0.f};
    float tip_position_z_history_[3] = {0.f, 0.f, 0.f};

    // Smoothed rotation axis (faster than before)
    float smoothed_rotation_axis_x_ = 0.f;
    float smoothed_rotation_axis_y_ = 0.f;
    float smoothed_rotation_axis_z_ = 1.f;
    // Time Constant approx 25ms at 100Hz
    static constexpr float kRotationAxisSmoothingCoefficient = 0.35f;

    // Smoothed staff long-axis (world frame), used for facing detection
    // during horizontal-plane spins.
    float smoothed_forward_axis_x_ = 1.f;
    float smoothed_forward_axis_y_ = 0.f;
    static constexpr float kForwardAxisSmoothingCoefficient = 0.20f;

    bool is_rotation_axis_vertical_ = false;
    float rotation_spin_direction_ = 1.f;

    bool was_rotation_axis_vertical_ = false;
    float previous_rotation_spin_direction_ = 1.f;

    // Reference-azimuth spin convention state (Bozendo family only).
    float reference_azimuth_x_ = 1.f;
    float reference_azimuth_y_ = 0.f;
    bool is_reference_azimuth_set_ = false;

    bool is_facing_north_ = true;
    bool previous_is_facing_north_ = true;
    static constexpr float kFacingHysteresisRatio =
        1.15f; // ~48deg / ~42deg switch points

    float accumulated_spin_degrees_ = 0.f;
    int continuous_spin_count_ = 0;

    // Peak / thrust detection
    // A peak = linear acceleration along the staff long axis in world frame
    // with simultaneously low rotation.
    // rotation_gate: gyroscope_magnitude must be below kPeakMaximumGyroscope
    // to qualify as a thrust
    float axial_thrust_peak_envelope_ = 0.f;
    float previous_axial_acceleration_ = 0.f;
    float previous_axial_jerk_ = 0.f;
    float thrust_cooldown_seconds_ = 0.f;
    static constexpr float kPeakMaximumGyroscope =
        90.f; // deg/s - lowered to distinguish from circle spins
    static constexpr float kPeakAxialAccelerationThreshold =
        1.5f; // g - minimum axial acceleration to qualify
    static constexpr float kPeakDecayCoefficient =
        0.94f; // per packet approx 100Hz -> approx 150ms half-life
    static constexpr float kThrustCooldownDurationSeconds = 0.2f;
};
