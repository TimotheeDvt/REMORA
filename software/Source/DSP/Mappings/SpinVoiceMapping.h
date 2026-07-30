#pragma once

#include "../IMappingStrategy.h"
#include "../StaffMotionAnalyzer.h"
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

    StaffMotionAnalyzer motion_;

    static constexpr float kRootFrequencyHz = 130.81f; // C3

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
};
