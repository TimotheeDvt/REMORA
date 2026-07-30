#pragma once

#include "../IMappingStrategy.h"
#include "../StaffMotionAnalyzer.h"
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

    StaffMotionAnalyzer motion_;

    static constexpr float kRootFrequencyHz = 130.81f; // C3

    static constexpr float kChordVoicing[3] = { 7.f, 12.f, 19.f };

    float noise_envelope_ = 0.f;
    static constexpr float kNoiseDecayCoefficient = 0.9985f;

    float current_base_semitones_ = 0.f;
    float target_base_semitones_ = 0.f;

    float smoothed_lpf_cutoff_hz_ = 20000.f;

    void applyPitchAndChordToOutput(MappingOutput& mapping_output, const StaffSoundParams& input_parameters);
    void applyVoicesToOutput(MappingOutput& mapping_output, float melody_gain);
    void applyMasterGainToOutput(MappingOutput& mapping_output, float laban_weight, float motion_gate);
    void applyTimbreToOutput(MappingOutput& mapping_output, float laban_space_focus, float laban_weight);
    void applyNoiseToOutput(MappingOutput& mapping_output, float suddenness_normalized);
    void applyModulationToOutput(MappingOutput& mapping_output, float laban_weight, float flow_bound, float flow_free);
    void applyStereoPanToOutput(MappingOutput& mapping_output, float flow_free, float axis_x, float axis_y);
};
