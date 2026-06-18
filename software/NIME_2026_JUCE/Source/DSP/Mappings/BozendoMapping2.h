#pragma once

#include "../IMappingStrategy.h"
#include <JuceHeader.h>
#include <array>
#include "../MathHelpers.h"
#include <cmath>

class BozendoMapping2 : public IMappingStrategy {
public:
    const char* getName() const override { return "Bozendo 2"; }
    void prepare(double sample_rate_hz) override;
    void process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) override;

private:
    double sample_rate_hz_ = 44100.0;
    float  delta_time_seconds_ = 1.0f / 100.0f;
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
    static constexpr float kGyroscopeFloor = 30.0f;
    static constexpr float kGyroscopeCeiling = 750.0f;

    static constexpr float kRootFrequencyHz = 130.81f; // C3

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

    bool  is_rotation_axis_vertical_ = false;
    float rotation_spin_direction_  = 1.f;

    float reference_azimuth_x_ = 1.f;
    float reference_azimuth_y_ = 0.f;
    bool  is_reference_azimuth_set_ = false;

    bool  was_rotation_axis_vertical_ = false;
    float previous_rotation_spin_direction_ = 1.f;

    static constexpr float kChordVoicing[3] = { 7.f, 12.f, 19.f };

    float noise_envelope_ = 0.f;
    static constexpr float kNoiseDecayCoefficient = 0.9985f;

    // Peak / thrust detection
    // A peak = linear acceleration along the staff long axis in world frame
    // with simultaneously low rotation.
    // rotation_gate: gyroscope_magnitude must be below kPeakMaximumGyroscope to qualify as a thrust
    float axial_thrust_peak_envelope_ = 0.f;
    float previous_axial_acceleration_ = 0.f;
    float previous_axial_jerk_ = 0.f;
    float thrust_cooldown_seconds_ = 0.f;
    static constexpr float kPeakMaximumGyroscope = 90.f;  // deg/s - lowered to distinguish from circle spins
    static constexpr float kPeakAxialAccelerationThreshold = 1.5f;   // g - minimum axial acceleration to qualify
    static constexpr float kPeakDecayCoefficient = 0.94f;  // per packet approx 100Hz -> approx 150ms half-life
    static constexpr float kThrustCooldownDurationSeconds = 0.2f;

    float smoothed_output_gain_ = 0.f;

    float current_base_semitones_ = 0.f;
    float target_base_semitones_ = 0.f;

    void calculateDeltaTime();
    void updateTipPositionHistory(const StaffSoundParams& input_parameters, float& current_tip_x, float& current_tip_y, float& current_tip_z);
    void calculateRotationAxisAtMidpoint(float& axis_x, float& axis_y, float& axis_z);
    void updateGravityVector(const StaffSoundParams& input_parameters);
    void calculateDynamicAcceleration(const StaffSoundParams& input_parameters, float& dynamic_accel_x, float& dynamic_accel_y, float& dynamic_accel_z, float& dynamic_accel_magnitude);
    float integrateVelocityForLabanWeight(float dynamic_accel_x, float dynamic_accel_y, float dynamic_accel_z);
    void detectAxialThrustPeaks(const StaffSoundParams& input_parameters, float dynamic_accel_x, float dynamic_accel_y, float dynamic_accel_z, float tip_x, float tip_y, float tip_z, float gyroscope_magnitude);

    float updateLabanWeight(float velocity_magnitude);
    float updateLabanTime(float gyroscope_magnitude);
    float updateLabanSpace(const StaffSoundParams& input_parameters, float gyroscope_magnitude);
    void updateLabanFlow(float dynamic_acceleration_magnitude, float& flow_bound, float& flow_free);

    bool updateSpinClassification(float axis_x, float axis_y, float axis_z);

    void applyPitchAndChordToOutput(MappingOutput& mapping_output, const StaffSoundParams& input_parameters);
    void applyVoicesToOutput(MappingOutput& mapping_output, float melody_gain);
    void applyMasterGainToOutput(MappingOutput& mapping_output, float laban_weight, float motion_gate);
    void applyTimbreToOutput(MappingOutput& mapping_output, float laban_space_focus, float laban_weight);
    void applyNoiseToOutput(MappingOutput& mapping_output, float suddenness_normalized);
    void applyModulationToOutput(MappingOutput& mapping_output, float laban_weight, float flow_bound, float flow_free);
    void applyStereoPanToOutput(MappingOutput& mapping_output, float flow_free, float axis_x, float axis_y);
};