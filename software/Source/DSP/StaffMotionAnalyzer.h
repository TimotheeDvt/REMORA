#pragma once

#include "MathHelpers.h"
#include <JuceHeader.h>

struct StaffSoundParams;

// Shared engine every mapping composes, so only the signal-to-sound logic differs between mappings.
//
// Runs once per sensor packet (never per audio block), with the time step measured by the sensor.
// Smoothing and decay constants are written as "per reference tick" and converted to the actual
// time step, so behaviour does not depend on packet timing or on the host's buffer size.
class StaffMotionAnalyzer {
public:
    static constexpr float kGyroscopeFloor = 30.0f;
    static constexpr float kGyroscopeCeiling = 750.0f;

    // The constants here and in the preset graphs were tuned by ear with one evaluation per
    // 256-sample block at 48 kHz; that block period is the reference tick they are expressed in.
    static constexpr float kReferenceTickSeconds = 256.0f / 48000.0f;

    // Number of reference ticks in `deltaTimeSeconds`, clamped to stay sane across stream gaps.
    static float referenceTicks(float deltaTimeSeconds) noexcept {
        return juce::jlimit(0.0f, 40.0f, deltaTimeSeconds / kReferenceTickSeconds);
    }
    // One-pole coefficient `perTick` (per reference tick), converted to `ticks` reference ticks.
    static float coefficientFor(float perTick, float ticks) noexcept {
        return 1.0f - std::pow(1.0f - juce::jlimit(0.0f, 1.0f, perTick), ticks);
    }
    // Multiplicative decay `perTick` (per reference tick), converted to `ticks` reference ticks.
    static float decayFor(float perTick, float ticks) noexcept {
        return std::pow(juce::jlimit(0.0f, 1.0f, perTick), ticks);
    }

    // Only one of these may run per packet - both write the same shared state.
    enum class SpinConvention { ByAbsoluteComponent, ByReferenceAzimuth };

    // Excludes spin classification/facing/continuous-spin-count - the Graph Spin Classification node calls those directly.
    struct DerivedMotionFrame {
        float deltaTimeSeconds = 1.0f / 100.0f;
        float gyroscopeMagnitude = 0.f;
        float smoothedGyroscopeMagnitude = 0.f;
        // Rotation rate perpendicular to the staff (deg/s): spinning and swinging, not rolling the staff in the hands.
        float spinRateDegPerSec = 0.f;
        // Energy, onset, axis stability, jerk (abrupt/smooth). Field names follow the source-node ids.
        float labanWeight = 0.f;
        float labanTimeSuddenness = 0.f;
        float labanSpaceFocus = 0.f;
        float labanFlowBound = 0.f;
        float labanFlowFree = 0.f;
        bool  isMoving = false;
        float axialThrustPeakEnvelope = 0.f;
        // World-frame rotation axis (perpendicular part of the angular velocity, / kGyroscopeCeiling).
        float rotationAxisX = 0.f, rotationAxisY = 0.f, rotationAxisZ = 1.f;
        float tipX = 1.f, tipY = 0.f, tipZ = 0.f;
        float worldGyroZ = 0.f; // deg/s about the vertical
    };

    // Single source of truth for the pipeline every mapping used to duplicate. One call per packet.
    DerivedMotionFrame computeFrame(const StaffSoundParams& input_parameters);

    void prepare();

    float deltaTimeSeconds() const noexcept { return delta_time_seconds_; }

    float axialThrustPeakEnvelope() const noexcept {
        return axial_thrust_peak_envelope_;
    }
    float smoothedGyroscopeMagnitude() const noexcept {
        return smoothed_gyroscope_magnitude_;
    }

    // Two interchangeable direction conventions: ByAbsoluteComponent (Azimut family) vs ByReferenceAzimuth (Martial family).
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

    // Which of two lines on the floor the staff works along: the calibration's forward line, or the
    // line across it. It has no front/back distinction. Read from the spin plane during
    // vertical-plane spins, from where the staff points when it is not turning about the vertical,
    // and held otherwise (in particular during overhead spins, where the tip sweeps every heading).
    // Hysteresis avoids chatter at the boundary; returns true on the packet where facing changes.
    bool updateFacingClassification(bool is_moving);
    // true = across the forward line (the "facing" output's 1), false = along it.
    bool isFacingNorth() const noexcept { return is_facing_north_; }

    // Resets whenever spin classification changes; used by mappings that sweep a parameter via continuous spinning.
    int accumulateContinuousSpins(bool spin_classification_changed,
                                   float spin_rate_deg_per_sec);
    int continuousSpinCount() const noexcept { return continuous_spin_count_; }

private:
    void detectAxialThrustPeaks(float axial_acceleration, float gyroscope_magnitude);
    float updateLabanWeight(float velocity_magnitude);
    float updateLabanTime(float gyroscope_magnitude);
    float updateLabanSpace(const StaffSoundParams &input_parameters, float gyroscope_magnitude);
    void updateLabanFlow(float dynamic_acceleration_magnitude, float &flow_bound, float &flow_free);
    bool finishSpinClassification();

    float delta_time_seconds_ = 1.0f / 100.0f;
    float ticks_ = 1.0f; // reference ticks in the current packet's time step

    float velocity_x_ = 0.f;
    float velocity_y_ = 0.f;
    float velocity_z_ = 0.f;
    static constexpr float kVelocityDecayHalfLifeMilliseconds = 80.0f;

    float weight_envelope_ = 0.f;
    static constexpr float kWeightAttackCoefficient = 0.40f;
    static constexpr float kWeightReleaseCoefficient = 0.03f;

    float previous_gyroscope_magnitude_ = 0.f;
    bool has_previous_gyroscope_magnitude_ = false;
    float suddenness_envelope_ = 0.f;
    static constexpr float kSuddennessAttackCoefficient = 0.6f;
    static constexpr float kSuddennessSmoothingCoefficient = 0.15f;

    float previous_gyroscope_direction_x_ = 0.f;
    float previous_gyroscope_direction_y_ = 0.f;
    float previous_gyroscope_direction_z_ = 1.f;
    float axis_focus_ = 1.f;
    static constexpr float kAxisFocusSmoothingCoefficient = 0.10f;
    static constexpr float kAxisFocusRelaxCoefficient = 0.02f;

    float previous_dynamic_acceleration_magnitude_ = 0.f;
    bool has_previous_dynamic_acceleration_ = false;
    float flow_bound_envelope_ = 0.f;
    static constexpr float kFlowSmoothingCoefficient = 0.12f;

    float smoothed_gyroscope_magnitude_ = 0.f;
    static constexpr float kGyroscopeSmoothingCoefficient = 0.35f;

    float smoothed_rotation_axis_x_ = 0.f;
    float smoothed_rotation_axis_y_ = 0.f;
    float smoothed_rotation_axis_z_ = 1.f;
    static constexpr float kRotationAxisSmoothingCoefficient = 0.35f;

    // Current packet's tip and vertical rotation rate, for facing.
    float tip_x_ = 1.f, tip_y_ = 0.f, tip_z_ = 0.f;
    float world_gyro_z_ = 0.f;
    // Smoothed horizontal tip direction, used for facing when the staff is not turning about the vertical.
    float smoothed_forward_axis_x_ = 1.f;
    float smoothed_forward_axis_y_ = 0.f;
    static constexpr float kForwardAxisSmoothingCoefficient = 0.20f;
    static constexpr float kFacingMaxVerticalRateDegPerSec = 60.f; // above this the staff is turning; hold facing
    static constexpr float kFacingMinHorizontalTip = 0.5f;         // staff within 60deg of horizontal

    bool is_rotation_axis_vertical_ = false;
    float rotation_spin_direction_ = 1.f;

    bool was_rotation_axis_vertical_ = false;
    float previous_rotation_spin_direction_ = 1.f;

    // Reference-azimuth spin convention state (Martial family only).
    float reference_azimuth_x_ = 1.f;
    float reference_azimuth_y_ = 0.f;
    bool is_reference_azimuth_set_ = false;

    bool is_facing_north_ = true;
    static constexpr float kFacingHysteresisRatio =
        1.15f; // ~49deg / ~41deg switch points (atan(1.15), atan(1/1.15))

    float accumulated_spin_degrees_ = 0.f;
    int continuous_spin_count_ = 0;

    // Peak = axial dynamic acceleration (world frame) with simultaneously low rotation (gated by kPeakMaximumGyroscope).
    float axial_thrust_peak_envelope_ = 0.f;
    float previous_axial_acceleration_ = 0.f;
    float previous_axial_jerk_ = 0.f;
    bool has_previous_axial_ = false;
    float thrust_cooldown_seconds_ = 0.f;
    static constexpr float kPeakMaximumGyroscope =
        90.f; // deg/s - lowered to distinguish from circle spins
    static constexpr float kPeakAxialAccelerationThreshold =
        1.5f; // g - minimum axial acceleration to qualify
    static constexpr float kPeakDecayCoefficient = 0.94f; // per reference tick (~60 ms half-life)
    static constexpr float kThrustCooldownDurationSeconds = 0.2f;
};
