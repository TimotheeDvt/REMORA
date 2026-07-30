#pragma once

#include "../IMappingStrategy.h"
#include "../StaffMotionAnalyzer.h"
#include <JuceHeader.h>
#include <array>
#include "../MathHelpers.h"
#include <cmath>

// Variant of AzimutMapping: same body-motion mapping (Laban weight/time/space/flow,
// spin plane + direction, facing), but the LPF cutoff tracks the staff's
// rotation speed instead of the number of continuous spins in the same
// direction.
class AzimutPlusMapping : public IMappingStrategy {
public:
    AzimutPlusMapping();

    const char* getName() const override { return "Azimut+"; }
    void prepare(double sample_rate_hz) override;
    void process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) override;

private:
    MonitorParam& laban_weight_monitor_;
    MonitorParam& laban_time_monitor_;
    MonitorParam& speed_monitor_;
    MonitorParam& filter_cutoff_monitor_;
    MonitorParam& spin_plane_monitor_;
    MonitorParam& spin_direction_monitor_;
    MonitorParam& facing_monitor_;
    MonitorParam& thrust_peak_monitor_;

    double sample_rate_hz_ = 44100.0;

    StaffMotionAnalyzer motion_;

    static constexpr float kRootFrequencyHz = 130.81f; // C3

    // Root note table: [is_horizontal][is_ccw][is_east], semitone offsets relative to kRootFrequencyHz
    static constexpr float kRootSemitoneTable[2][2][2] = {
        // Vertical plane
        { { 0.f, 7.f },   // CW:  North=C(0),  East=G(7)
        { 4.f, 11.f } },// CCW: North=E(4),  East=B(11)
        // Horizontal plane
        { { 7.f, 7.f },  // CW:  North=G(7),  East=G(7)
        { 9.f, 9.f } } // CCW: North=A(9),  East=A(9)
    };

    static constexpr float kChordVoicing[3] = { 7.f, 12.f, 19.f };

    float noise_envelope_ = 0.f;
    static constexpr float kNoiseDecayCoefficient = 0.9985f;

    float current_base_semitones_ = 0.f;
    float target_base_semitones_ = 0.f;

    bool  was_moving_ = false;
    float movement_onset_envelope_ = 0.f;
    static constexpr float kMovementOnsetMorphSpeed = 0.5f;
    static constexpr float kMovementOnsetDecayCoefficient = 0.90f;

    float smoothed_lpf_cutoff_hz_ = 20000.f;

    void applyPitchAndChordToOutput(MappingOutput& mapping_output, const StaffSoundParams& input_parameters);
    void applyVoicesToOutput(MappingOutput& mapping_output, float melody_gain);
    void applyMasterGainToOutput(MappingOutput& mapping_output, float laban_weight, float motion_gate);
    void applyTimbreToOutput(MappingOutput& mapping_output, float laban_space_focus, float laban_weight);
    void applyNoiseToOutput(MappingOutput& mapping_output, float suddenness_normalized);
    void applyModulationToOutput(MappingOutput& mapping_output, float laban_weight, float flow_bound, float flow_free);
    void applyStereoPanToOutput(MappingOutput& mapping_output, float flow_free, float axis_x, float axis_y);
};
