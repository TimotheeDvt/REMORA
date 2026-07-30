#include "BozendoMapping.h"
#include "../BoStaffSynth.h"
#include "../../UI/DebugLog.h"
#include <cmath>
#include <algorithm>

constexpr float BozendoMapping::kPentatonicMinorScale[BozendoMapping::kNumberOfScaleSteps];
constexpr float BozendoMapping::kChordVoicing[3];

void BozendoMapping::prepare(double sample_rate_hz) {
    debug.print.green("BozendoMapping prepared at sample rate:", sample_rate_hz);
    sample_rate_hz_ = sample_rate_hz;
    motion_.prepare();

    current_scale_step_ = 0;
    noise_envelope_ = 0.f;
}

int BozendoMapping::convertGyroscopeMagnitudeToScaleStep(float gyroscope_magnitude) noexcept {
    float clamped_gyroscope = juce::jlimit(StaffMotionAnalyzer::kGyroscopeFloor, StaffMotionAnalyzer::kGyroscopeCeiling, gyroscope_magnitude);
    float normalized_gyroscope = (clamped_gyroscope - StaffMotionAnalyzer::kGyroscopeFloor) / (StaffMotionAnalyzer::kGyroscopeCeiling - StaffMotionAnalyzer::kGyroscopeFloor);
    float float_step = normalized_gyroscope * static_cast<float>(kNumberOfScaleSteps - 1);

    float hysteresis_normalized = kScaleHysteresis / (StaffMotionAnalyzer::kGyroscopeCeiling - StaffMotionAnalyzer::kGyroscopeFloor);
    float hysteresis_steps = hysteresis_normalized * static_cast<float>(kNumberOfScaleSteps - 1);

    int target_step = juce::jlimit(0, kNumberOfScaleSteps - 1, static_cast<int>(float_step + 0.5f));

    if (target_step > current_scale_step_) {
        if (float_step < static_cast<float>(current_scale_step_) + 0.5f + hysteresis_steps) {
            target_step = current_scale_step_;
        }
    } else if (target_step < current_scale_step_) {
        if (float_step > static_cast<float>(current_scale_step_) - 0.5f - hysteresis_steps) {
            target_step = current_scale_step_;
        }
    }
    return target_step;
}

void BozendoMapping::applyPitchAndChordToOutput(MappingOutput& mapping_output) {
    const float base_semitones = kPentatonicMinorScale[current_scale_step_];
    const float plane_offset   = motion_.isRotationAxisVertical() ? 12.0f : 0.0f;
    const float direction_offset = (motion_.rotationSpinDirection() > 0.f) ? 0.0f : -7.0f;
    mapping_output.rootHz = MathHelpers::convertSemitonesToHertz(base_semitones + plane_offset + direction_offset, kRootFrequencyHz);

    if (!motion_.isRotationAxisVertical()) {
        if (motion_.rotationSpinDirection() > 0.f) {
            // Chord quality : Major
            mapping_output.chordSemitones[0] = 4.f; mapping_output.chordSemitones[1] = 7.f;  mapping_output.chordSemitones[2] = 12.f;
        } else {
            // Chord quality : Minor
            mapping_output.chordSemitones[0] = 3.f; mapping_output.chordSemitones[1] = 7.f;  mapping_output.chordSemitones[2] = 12.f;
        }
    } else {
        if (motion_.rotationSpinDirection() > 0.f) {
            // Chord quality : 7th
            mapping_output.chordSemitones[0] = 4.f; mapping_output.chordSemitones[1] = 7.f;  mapping_output.chordSemitones[2] = 10.f;
        } else {
            // Chord quality : Diminished7
            mapping_output.chordSemitones[0] = 3.f; mapping_output.chordSemitones[1] = 6.f;  mapping_output.chordSemitones[2] = 9.f;
        }
    }
}

void BozendoMapping::applyVoicesToOutput(MappingOutput& mapping_output, float laban_weight, float melody_gain) {
    mapping_output.numVoices = (laban_weight < 0.25f) ? 1 : (laban_weight < 0.55f) ? 2 : (laban_weight < 0.80f) ? 3 : 4;

    mapping_output.voiceGain[0] = melody_gain;
    mapping_output.voiceGain[1] = juce::jlimit(0.0f, 1.0f, (laban_weight - 0.25f) * 4.0f) * 0.8f;
    mapping_output.voiceGain[2] = juce::jlimit(0.0f, 1.0f, (laban_weight - 0.55f) * 3.3f) * 0.7f;
    mapping_output.voiceGain[3] = juce::jlimit(0.f, 1.f, (laban_weight - 0.80f) * 5.0f) * 0.6f;
}

void BozendoMapping::applyMasterGainToOutput(MappingOutput& mapping_output, float laban_weight, float motion_gate) {
    mapping_output.masterGain = motion_gate * (0.05f + laban_weight * 0.70f);
    // peak punches through the gain gate
    mapping_output.masterGain = juce::jlimit(0.f, 1.f, mapping_output.masterGain + motion_.axialThrustPeakEnvelope() * 0.6f);
}

void BozendoMapping::applyTimbreToOutput(MappingOutput& mapping_output, float laban_space_focus, float laban_weight) {
    constexpr float kDirectGain[6]   = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
    constexpr float kIndirectGain[6] = { 1.0f, 0.10f, 0.50f, 0.05f, 0.30f, 0.03f };
    for (int i = 0; i < 6; ++i) {
        mapping_output.partialAmps[i] = kDirectGain[i] * laban_space_focus + kIndirectGain[i] * (1.f - laban_space_focus);
    }
    mapping_output.driveAmt = laban_weight * (1.f - laban_space_focus * 0.7f) * 2.5f;

    // peak adds bright transient: boost upper partials
    float peak_brightness = motion_.axialThrustPeakEnvelope() * 0.8f;
    mapping_output.partialAmps[3] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[3] + peak_brightness * 0.5f);
    mapping_output.partialAmps[4] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[4] + peak_brightness * 0.7f);
    mapping_output.partialAmps[5] = juce::jlimit(0.f, 1.f, mapping_output.partialAmps[5] + peak_brightness * 0.9f);
    mapping_output.driveAmt = juce::jlimit(0.f, 4.f, mapping_output.driveAmt + motion_.axialThrustPeakEnvelope() * 2.0f);
}

void BozendoMapping::applyNoiseToOutput(MappingOutput& mapping_output, float suddenness_normalized) {
    if (suddenness_normalized > 0.3f) {
        noise_envelope_ = juce::jlimit(0.0f, 1.0f, noise_envelope_ + (suddenness_normalized - 0.3f) * 1.5f);
    }
    // peak injects its own sharp noise burst
    noise_envelope_ = juce::jlimit(0.f, 1.f, noise_envelope_ + motion_.axialThrustPeakEnvelope() * 0.5f);
    noise_envelope_ *= kNoiseDecayCoefficient;

    mapping_output.noiseAmount = noise_envelope_ * 0.4f;
    mapping_output.noiseLpCoef = 1.f - (0.2f + suddenness_normalized * 0.4f + motion_.axialThrustPeakEnvelope() * 0.4f);
}

void BozendoMapping::applyModulationToOutput(MappingOutput& mapping_output, float laban_weight, float flow_bound, float flow_free) {
    mapping_output.vibratoDepth  = flow_free * laban_weight * 0.020f;
    mapping_output.vibratoRateHz = 4.5f + motion_.smoothedGyroscopeMagnitude() * 0.005f;
    mapping_output.tremoloDepth  = flow_bound * laban_weight * 0.30f;
    mapping_output.tremoloRateHz = 3.0f + flow_bound * 4.0f;
}

void BozendoMapping::applyStereoPanToOutput(MappingOutput& mapping_output, float flow_free, float axis_x, float axis_y) {
    float pan_bias = 0.f;
    float perpendicular_magnitude = std::sqrt(axis_x * axis_x + axis_y * axis_y);
    if (perpendicular_magnitude > 1e-3f) {
        float azimuth = std::atan2(axis_y, axis_x);
        pan_bias = juce::jlimit(-0.2f, 0.2f, azimuth / juce::MathConstants<float>::pi * 0.2f);
    }
    const float spread = 0.3f + flow_free * 0.5f;
    mapping_output.panL[0] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f + pan_bias);
    mapping_output.panR[0] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f - pan_bias);
    mapping_output.panL[1] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.10f + pan_bias);
    mapping_output.panR[1] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.10f - pan_bias);
    mapping_output.panL[2] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f + pan_bias);
    mapping_output.panR[2] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f - pan_bias);
    mapping_output.panL[3] = juce::jlimit(0.f, 1.f, 0.5f - spread * 0.40f + pan_bias);
    mapping_output.panR[3] = juce::jlimit(0.f, 1.f, 0.5f + spread * 0.40f - pan_bias);
}

void BozendoMapping::process(const StaffSoundParams& input_parameters, MappingOutput& mapping_output) {
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
    if (is_moving) {
        bool spin_changed = motion_.updateSpinClassificationByReferenceAzimuth(rotation_axis_x, rotation_axis_y, rotation_axis_z);
        if (spin_changed) {
            debug.print.cyan(
                motion_.isRotationAxisVertical() ? "VERTICAL" : "HORIZONTAL",
                motion_.rotationSpinDirection() > 0.f ? "COUNTER_CLOCKWISE_OR_FORWARD" : "CLOCKWISE_OR_BACKWARD"
            );
        }
        current_scale_step_ = convertGyroscopeMagnitudeToScaleStep(motion_.smoothedGyroscopeMagnitude());
    }

    applyPitchAndChordToOutput(mapping_output);

    float melody_gain = is_moving ? juce::jlimit(0.2f, 1.0f, 0.2f + motion_.smoothedGyroscopeMagnitude() / StaffMotionAnalyzer::kGyroscopeCeiling * 0.8f) : 0.0f;
    applyVoicesToOutput(mapping_output, laban_weight, melody_gain);

    float motion_gate = juce::jlimit(0.0f, 1.0f, (motion_.smoothedGyroscopeMagnitude() - StaffMotionAnalyzer::kGyroscopeFloor * 0.5f) / (StaffMotionAnalyzer::kGyroscopeFloor * 1.5f));
    applyMasterGainToOutput(mapping_output, laban_weight, motion_gate);

    applyTimbreToOutput(mapping_output, laban_space_focus, laban_weight);
    applyNoiseToOutput(mapping_output, laban_time_suddenness_normalized);
    applyModulationToOutput(mapping_output, laban_weight, laban_flow_bound, laban_flow_free);
    applyStereoPanToOutput(mapping_output, laban_flow_free, rotation_axis_x, rotation_axis_y);
}
