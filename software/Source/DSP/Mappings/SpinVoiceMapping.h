#pragma once

#include "../IMappingStrategy.h"
#include <JuceHeader.h>
#include "../MathHelpers.h"
#include <cmath>

// Spin plane + direction select which of 4 voices is currently being
// "played"; the other 3 hold their last pitch and gain exactly where they
// were left. CW+Vertical, CCW+Vertical, CW+Horizontal and CCW+Horizontal
// each own one voice. While a voice is active, staff speed bends its pitch
// upward and Laban weight drives its gain; switching to a different spin
// combo freezes that voice and hands control to the next one, so moving
// through all 4 combos builds up a sustained chord one note at a time.
// Facing (used by AzimutMapping) is deliberately not part of this mapping.
class SpinVoiceMapping : public IMappingStrategy {
public:
    SpinVoiceMapping();

    const char* getName() const override { return "Spin Voices"; }
    void prepare(double sample_rate_hz) override;
    void process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) override;

private:
    static constexpr int kNumControllableVoices = 4;

    MonitorParam& laban_weight_monitor_;
    MonitorParam& speed_monitor_;
    MonitorParam& spin_plane_monitor_;
    MonitorParam& spin_direction_monitor_;
    MonitorParam& active_voice_monitor_;

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
    // Time constant approx 22ms attack, 1700ms release (matches AzimutMapping).
    static constexpr float kWeightAttackCoefficient = 0.40f;
    static constexpr float kWeightReleaseCoefficient = 0.006f;

    float smoothed_gyroscope_magnitude_ = 0.f;
    static constexpr float kGyroscopeSmoothingCoefficient = 0.35f;
    static constexpr float kGyroscopeFloor = 30.0f;
    static constexpr float kGyroscopeCeiling = 750.0f;

    static constexpr float kRootFrequencyHz = 130.81f; // C3

    // Tip position history - 3 frames, ring buffer, used to find the
    // instantaneous rotation axis (spin plane + direction).
    float tip_position_x_history_[3] = { 1.f, 1.f, 1.f };
    float tip_position_y_history_[3] = { 0.f, 0.f, 0.f };
    float tip_position_z_history_[3] = { 0.f, 0.f, 0.f };

    float smoothed_rotation_axis_x_ = 0.f;
    float smoothed_rotation_axis_y_ = 0.f;
    float smoothed_rotation_axis_z_ = 1.f;
    static constexpr float kRotationAxisSmoothingCoefficient = 0.35f;

    bool  is_rotation_axis_vertical_ = false;
    float rotation_spin_direction_ = 1.f;
    bool  was_rotation_axis_vertical_ = false;
    float previous_rotation_spin_direction_ = 1.f;

    int active_voice_index_ = 0;

    // Base chord tone (semitones above root) each voice sits on: with all 4
    // held, they spell out a C major chord across two octaves.
    static constexpr float kVoiceBaseSemitones[kNumControllableVoices] = { 0.f, 7.f, 12.f, 19.f };
    // How far speed can bend the active voice above its base tone.
    static constexpr float kSpeedPitchBendSemitones = 12.f;

    // Persistent per-voice pitch/gain: only the active voice's entry is
    // updated each frame, so the other three hold their last value.
    float voice_semitones_[kNumControllableVoices] = { 0.f, 7.f, 12.f, 19.f };
    float voice_gain_[kNumControllableVoices] = { 0.f, 0.f, 0.f, 0.f };
    static constexpr float kVoicePitchMorphCoefficient = 0.08f;
    static constexpr float kVoiceGainMorphCoefficient = 0.15f;

    void calculateDeltaTime();
    void updateTipPositionHistory(const StaffSoundParams& input_parameters, float& current_tip_x, float& current_tip_y, float& current_tip_z);
    void calculateRotationAxisAtMidpoint(float& axis_x, float& axis_y, float& axis_z);
    void updateGravityVector(const StaffSoundParams& input_parameters);
    float updateLabanWeight(const StaffSoundParams& input_parameters);
    void updateSpinClassification(float axis_x, float axis_y, float axis_z);
};
