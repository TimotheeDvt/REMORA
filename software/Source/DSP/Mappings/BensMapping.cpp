#include "BensMapping.h"
#include "../BoStaffSynth.h"
#include "../MathHelpers.h"
#include "../../UI/DebugLog.h"
#include <cmath>

BensMapping::BensMapping()
    : speed_monitor_(addMonitorParam("Gain", "Speed", 0.0f, 1000.0f)),
      azimut_amount_monitor_(addMonitorParam("LPF Cutoff", "Azimut Amount", 0.0f, 1.0f))
{
}

void BensMapping::prepare(double sampleRate) {
    debug.print.green("BensMapping prepared at sample rate:", sampleRate);
    azimut_.prepare(sampleRate);
    smoothed_gyroscope_magnitude_ = 0.f;
    current_simple_semitones_ = 0.f;
}

void BensMapping::processSimpleMelody(const StaffSoundParams& in, MappingOutput& out) {
    constexpr float pi = 3.14159265f;
    // Major pentatonic scale degrees, walked over two octaves by pitch.
    static constexpr float kScaleSemitones[5] = { 0.f, 2.f, 4.f, 7.f, 9.f };

    float pitchNorm = juce::jlimit(0.0f, 1.0f, (in.pitch + pi * 0.5f) / pi);
    int scaleIndex = juce::jlimit(0, 9, static_cast<int>(pitchNorm * 10.0f));
    float octaveOffset = (scaleIndex >= 5) ? 12.f : 0.f;
    float targetSemitones = kScaleSemitones[scaleIndex % 5] + octaveOffset;

    // Smooth note changes so the melody doesn't zipper between pitch samples.
    current_simple_semitones_ = MathHelpers::applyOnePoleFilter(current_simple_semitones_, targetSemitones, 0.15f);
    out.rootHz = MathHelpers::convertSemitonesToHertz(current_simple_semitones_, kRootFrequencyHz);

    // Same voicing as Azimut (octave + fifth) so the two mappings blend cleanly.
    out.chordSemitones[0] = 12.f;
    out.chordSemitones[1] = 7.f;
    out.chordSemitones[2] = -12.f;

    float rollNorm = juce::jlimit(0.0f, 1.0f, std::abs(in.roll) / pi);
    out.masterGain = 0.05f + rollNorm * 0.20f;

    out.numVoices = 4;
    out.voiceGain[0] = 1.0f;
    out.voiceGain[1] = 0.8f;
    out.voiceGain[2] = 0.7f;
    out.voiceGain[3] = 0.6f;

    constexpr float kConstantGain[6] = { 1.0f, 0.5f, 0.25f, 0.1f, 0.05f, 0.02f };
    for (int i = 0; i < 6; ++i) out.partialAmps[i] = kConstantGain[i];
    out.driveAmt = 0.0f;

    // Yaw pans the melody across the stereo field.
    float yawNorm = juce::jlimit(-1.0f, 1.0f, in.yaw / pi);
    float pan = juce::jlimit(0.f, 1.f, 0.5f + yawNorm * 0.35f);
    for (int i = 0; i < 4; ++i) {
        out.panL[i] = pan;
        out.panR[i] = 1.0f - pan;
    }

    out.vibratoDepth  = 0.0f;
    out.vibratoRateHz = 5.0f;
    out.tremoloDepth  = 0.0f;
    out.tremoloRateHz = 4.0f;
    out.noiseAmount   = 0.0f;
    out.noiseLpCoef   = 0.5f;
    out.lpfCutoffHz   = 8000.0f;
}

void BensMapping::blendMappingOutput(const MappingOutput& simple, const MappingOutput& azimut, float azimutAmount, MappingOutput& out) {
    const auto lerp = [azimutAmount](float a, float b) { return a + (b - a) * azimutAmount; };

    out.rootHz = lerp(simple.rootHz, azimut.rootHz);
    for (int i = 0; i < 3; ++i) out.chordSemitones[i] = lerp(simple.chordSemitones[i], azimut.chordSemitones[i]);
    out.numVoices = azimut.numVoices;

    out.masterGain = lerp(simple.masterGain, azimut.masterGain);
    for (int i = 0; i < 4; ++i) out.voiceGain[i] = lerp(simple.voiceGain[i], azimut.voiceGain[i]);

    for (int i = 0; i < 6; ++i) out.partialAmps[i] = lerp(simple.partialAmps[i], azimut.partialAmps[i]);
    out.driveAmt = lerp(simple.driveAmt, azimut.driveAmt);

    out.vibratoDepth  = lerp(simple.vibratoDepth, azimut.vibratoDepth);
    out.vibratoRateHz = lerp(simple.vibratoRateHz, azimut.vibratoRateHz);
    out.tremoloDepth  = lerp(simple.tremoloDepth, azimut.tremoloDepth);
    out.tremoloRateHz = lerp(simple.tremoloRateHz, azimut.tremoloRateHz);

    out.noiseAmount = lerp(simple.noiseAmount, azimut.noiseAmount);
    out.noiseLpCoef = lerp(simple.noiseLpCoef, azimut.noiseLpCoef);

    for (int i = 0; i < 4; ++i) {
        out.panL[i] = lerp(simple.panL[i], azimut.panL[i]);
        out.panR[i] = lerp(simple.panR[i], azimut.panR[i]);
    }

    out.lpfCutoffHz = lerp(simple.lpfCutoffHz, azimut.lpfCutoffHz);
}

void BensMapping::process(const StaffSoundParams& in, MappingOutput& out) {
    const float gyroscope_magnitude = std::sqrt(in.gx * in.gx + in.gy * in.gy + in.gz * in.gz);
    smoothed_gyroscope_magnitude_ = MathHelpers::applyOnePoleFilter(smoothed_gyroscope_magnitude_, gyroscope_magnitude, kGyroscopeSmoothingCoefficient);

    const float bandStart = kGateSpeedThresholdDegPerSec - kGateSpeedBandDegPerSec * 0.5f;
    const float azimutAmount = juce::jlimit(0.0f, 1.0f, (smoothed_gyroscope_magnitude_ - bandStart) / kGateSpeedBandDegPerSec);

    speed_monitor_.value.store(smoothed_gyroscope_magnitude_, std::memory_order_relaxed);
    azimut_amount_monitor_.value.store(azimutAmount, std::memory_order_relaxed);

    MappingOutput simpleOutput;
    processSimpleMelody(in, simpleOutput);

    MappingOutput azimutOutput;
    azimut_.process(in, azimutOutput);

    blendMappingOutput(simpleOutput, azimutOutput, azimutAmount, out);
}
