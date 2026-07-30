#include "BozendoMapping2.h"
#include "../BoStaffSynth.h"
#include "../../UI/DebugLog.h"
#include <cmath>
#include <algorithm>

constexpr float BozendoMapping2::kChordVoicing[3];

void BozendoMapping2::prepare(double sample_rate_hz) {
    debug.print.green("BozendoMapping2 prepared at sample rate:", sample_rate_hz);
    sample_rate_hz_ = sample_rate_hz;
    motion_.prepare();

    noise_envelope_ = 0.f;
    current_base_semitones_ = 0.f;
    target_base_semitones_ = 0.f;

    smoothed_lpf_cutoff_hz_ = 20000.f;
}

void BozendoMapping2::applyPitchAndChordToOutput(MappingOutput& mapping_output, const StaffSoundParams& input_parameters) {
    juce::ignoreUnused(input_parameters);

    if (motion_.isRotationAxisVertical()) {
        if (motion_.rotationSpinDirection() < 0.f) {
            // CW Vertical: 1st Note (C)
            target_base_semitones_ = 0.f;
        } else {
            // CCW Vertical: 2nd Note (E)
            target_base_semitones_ = 4.f;
        }
    } else {
        if (motion_.rotationSpinDirection() < 0.f) {
            // CW Horizontal: 3rd Note (G)
            target_base_semitones_ = 7.f;
        } else {
            // CCW Horizontal: 4th Note (A)
            target_base_semitones_ = 9.f;
        }
    }

    // Morph speed scales with how fast the staff is moving
    float morph_speed = juce::jlimit(0.005f, 0.2f, motion_.smoothedGyroscopeMagnitude() / 2000.0f);
    current_base_semitones_ = MathHelpers::applyOnePoleFilter(current_base_semitones_, target_base_semitones_, morph_speed);

    // Instead of a chord, we only play octaves of the root note to maintain the same pitch class.
    mapping_output.chordSemitones[0] = 12.f;  // +1 Octave
    mapping_output.chordSemitones[1] = 7.f;  // +5th
    mapping_output.chordSemitones[2] = -12.f; // -1 Octave

    mapping_output.rootHz = MathHelpers::convertSemitonesToHertz(current_base_semitones_, kRootFrequencyHz);
}

void BozendoMapping2::applyVoicesToOutput(MappingOutput& mapping_output, float melody_gain) {
    mapping_output.numVoices = 4;
    mapping_output.voiceGain[0] = melody_gain;

    // All voices play together, with gains relative to the melody gain.
    mapping_output.voiceGain[1] = melody_gain * 0.8f;
    mapping_output.voiceGain[2] = melody_gain * 0.7f;
    mapping_output.voiceGain[3] = melody_gain * 0.6f;
}

void BozendoMapping2::applyMasterGainToOutput(MappingOutput& mapping_output, float laban_weight, float motion_gate) {
    mapping_output.masterGain = motion_gate * (0.05f + laban_weight * 0.70f);
    // peak punches through the gain gate
    mapping_output.masterGain = juce::jlimit(0.f, 1.f, mapping_output.masterGain + motion_.axialThrustPeakEnvelope() * 0.6f);
}

void BozendoMapping2::applyTimbreToOutput(MappingOutput& mapping_output, float laban_space_focus, float laban_weight) {
    juce::ignoreUnused(laban_space_focus);
    constexpr float kConstantGain[6] = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
    for (int i = 0; i < 6; ++i) {
        mapping_output.partialAmps[i] = kConstantGain[i];
    }
    mapping_output.driveAmt = 1.0f + laban_weight * 1.0f;

    // peak adds bright transient: boost upper partials
    float peak_brightness = motion_.axialThrustPeakEnvelope() * 0.8f;
    mapping_output.partialAmps[3] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[3] + peak_brightness * 0.5f);
    mapping_output.partialAmps[4] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[4] + peak_brightness * 0.7f);
    mapping_output.partialAmps[5] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[5] + peak_brightness * 0.9f);
    mapping_output.driveAmt = juce::jlimit(0.f, 4.f, mapping_output.driveAmt + motion_.axialThrustPeakEnvelope() * 2.0f);
}

void BozendoMapping2::applyNoiseToOutput(MappingOutput& mapping_output, float suddenness_normalized) {
    if (suddenness_normalized > 0.3f) {
        noise_envelope_ = juce::jlimit(0.0f, 1.0f, noise_envelope_ + (suddenness_normalized - 0.3f) * 1.5f);
    }
    // peak injects its own sharp noise burst
    noise_envelope_ = juce::jlimit(0.f, 1.f, noise_envelope_ + motion_.axialThrustPeakEnvelope() * 0.5f);
    noise_envelope_ *= kNoiseDecayCoefficient;

    mapping_output.noiseAmount = noise_envelope_ * 0.4f;
    mapping_output.noiseLpCoef = 1.f - (0.2f + suddenness_normalized * 0.4f + motion_.axialThrustPeakEnvelope() * 0.4f);
}

void BozendoMapping2::applyModulationToOutput(MappingOutput& mapping_output, float laban_weight, float flow_bound, float flow_free) {
    juce::ignoreUnused(laban_weight, flow_bound, flow_free);
    mapping_output.vibratoDepth  = 0.0f;
    mapping_output.vibratoRateHz = 0.0f;
    mapping_output.tremoloDepth  = 0.0f;
    mapping_output.tremoloRateHz = 0.0f;
}

void BozendoMapping2::applyStereoPanToOutput(MappingOutput& mapping_output, float flow_free, float axis_x, float axis_y) {
    juce::ignoreUnused(flow_free, axis_x, axis_y);
    const float spread = 0.5f;
    mapping_output.panL[0] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f);
    mapping_output.panR[0] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f);
    mapping_output.panL[1] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f);
    mapping_output.panR[1] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f);
    mapping_output.panL[2] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f);
    mapping_output.panR[2] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f);
    mapping_output.panL[3] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f);
    mapping_output.panR[3] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f);
}

void BozendoMapping2::process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) {
    motion_.calculateDeltaTime();

    float tip_x, tip_y, tip_z;
    motion_.updateTipPositionHistory(input_parameters, tip_x, tip_y, tip_z);

    float rotation_axis_x, rotation_axis_y, rotation_axis_z;
    motion_.calculateRotationAxisAtMidpoint(rotation_axis_x, rotation_axis_y, rotation_axis_z);

    motion_.updateGravityVector(input_parameters);

    float dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_magnitude;
    motion_.calculateDynamicAcceleration(input_parameters, dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, dynamic_accel_magnitude);

    float velocity_magnitude = motion_.integrateVelocityForLabanWeight(dynamic_accel_x, dynamic_accel_y, dynamic_accel_z);

    const float gyroscope_magnitude = std::sqrt(input_parameters.gx * input_parameters.gx + input_parameters.gy * input_parameters.gy + input_parameters.gz * input_parameters.gz);
    motion_.updateGyroscopeMagnitude(gyroscope_magnitude);

    motion_.detectAxialThrustPeaks(input_parameters, dynamic_accel_x, dynamic_accel_y, dynamic_accel_z, tip_x, tip_y, tip_z, gyroscope_magnitude);

    float laban_weight = motion_.updateLabanWeight(velocity_magnitude);
    float laban_time_suddenness_normalized = motion_.updateLabanTime(gyroscope_magnitude);
    float laban_space_focus = motion_.updateLabanSpace(input_parameters, gyroscope_magnitude);

    float laban_flow_bound, laban_flow_free;
    motion_.updateLabanFlow(dynamic_accel_magnitude, laban_flow_bound, laban_flow_free);

    const bool is_moving = motion_.smoothedGyroscopeMagnitude() > StaffMotionAnalyzer::kGyroscopeFloor;
    bool spin_changed = false;
    if (is_moving) {
        spin_changed = motion_.updateSpinClassificationByReferenceAzimuth(rotation_axis_x, rotation_axis_y, rotation_axis_z);
        motion_.accumulateContinuousSpins(spin_changed, gyroscope_magnitude);
    }

    applyPitchAndChordToOutput(mapping_output, input_parameters);

    if (spin_changed) {
        const char* note_name = "";
        if (motion_.isRotationAxisVertical()) {
            note_name = (motion_.rotationSpinDirection() < 0.f) ? "C" : "E";
        } else {
            note_name = (motion_.rotationSpinDirection() < 0.f) ? "G" : "A";
        }
        debug.print.cyan(
            motion_.isRotationAxisVertical() ? "VERTICAL" : "HORIZONTAL",
            motion_.rotationSpinDirection() > 0.f ? "COUNTER_CLOCKWISE_OR_FORWARD" : "CLOCKWISE_OR_BACKWARD",
            "| Morphing to:", note_name
        );
    }

    float melody_gain = is_moving ? 1.0f : 0.0f;
    applyVoicesToOutput(mapping_output, melody_gain);

    float motion_gate = juce::jlimit(0.0f, 1.0f, (motion_.smoothedGyroscopeMagnitude() - StaffMotionAnalyzer::kGyroscopeFloor * 0.5f) / (StaffMotionAnalyzer::kGyroscopeFloor * 1.5f));
    applyMasterGainToOutput(mapping_output, laban_weight, motion_gate);

    applyTimbreToOutput(mapping_output, laban_space_focus, laban_weight);
    applyNoiseToOutput(mapping_output, laban_time_suddenness_normalized);
    applyModulationToOutput(mapping_output, laban_weight, laban_flow_bound, laban_flow_free);
    applyStereoPanToOutput(mapping_output, laban_flow_free, rotation_axis_x, rotation_axis_y);

    // Morph global LPF cutoff using a sine wave driven by the spin count
    // Increased multiplier to 1.5f so it sweeps back and forth faster
    float spin_phase = static_cast<float>(motion_.continuousSpinCount()) * 1.5f;
    float sine_val = std::sin(spin_phase);

    // Map sine wave output (-1.0 to 1.0) to a frequency range (400 Hz to 20000 Hz)
    float target_lpf_cutoff = juce::jmap(sine_val, -1.0f, 1.0f, 400.0f, 20000.0f);
    // Lowered smoothing coefficient to 0.03f for a more fluid glide
    smoothed_lpf_cutoff_hz_ = MathHelpers::applyOnePoleFilter(smoothed_lpf_cutoff_hz_, target_lpf_cutoff, 0.03f);

    mapping_output.lpfCutoffHz = smoothed_lpf_cutoff_hz_;
}
