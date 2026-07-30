#pragma once

#include "../IMappingStrategy.h"
#include "../StaffMotionAnalyzer.h"
#include <JuceHeader.h>
#include <array>
#include "../MathHelpers.h"
#include <cmath>

class BozendoMapping : public IMappingStrategy {
public:
    const char* getName() const override { return "Bozendo"; }
    void prepare(double sample_rate_hz) override;
    void process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) override;

private:
    double sample_rate_hz_ = 44100.0;

    StaffMotionAnalyzer motion_;

    int current_scale_step_ = 0;
    static constexpr float kScaleHysteresis = 8.0f;

    static constexpr int kNumberOfScaleSteps = 10;
    static constexpr float kPentatonicMinorScale[kNumberOfScaleSteps] = {
        -12.f, -9.f, -5.f, -2.f, 0.f,
          3.f,  7.f, 10.f, 12.f, 15.f
    };
    static constexpr float kRootFrequencyHz = 146.83f;

    static constexpr float kChordVoicing[3] = { 7.f, 12.f, 19.f };

    float noise_envelope_ = 0.f;
    static constexpr float kNoiseDecayCoefficient = 0.9985f;

    int convertGyroscopeMagnitudeToScaleStep(float gyroscope_magnitude) noexcept;

    void applyPitchAndChordToOutput(MappingOutput& mapping_output);
    void applyVoicesToOutput(MappingOutput& mapping_output, float laban_weight, float melody_gain);
    void applyMasterGainToOutput(MappingOutput& mapping_output, float laban_weight, float motion_gate);
    void applyTimbreToOutput(MappingOutput& mapping_output, float laban_space_focus, float laban_weight);
    void applyNoiseToOutput(MappingOutput& mapping_output, float suddenness_normalized);
    void applyModulationToOutput(MappingOutput& mapping_output, float laban_weight, float flow_bound, float flow_free);
    void applyStereoPanToOutput(MappingOutput& mapping_output, float flow_free, float axis_x, float axis_y);
};
