#include "SpinFilterMapping.h"
#include "../BoStaffSynth.h"

void SpinFilterMapping::process(const StaffSoundParams &params, MappingOutput &out) {
    // Pitch mapping from gyroscope magnitude
    float gyroMag = std::sqrt(params.gx * params.gx + params.gy * params.gy + params.gz * params.gz);
    float accelMag = std::sqrt(params.ax * params.ax + params.ay * params.ay + params.az * params.az);

    // Map gyro to scale step, clamped to 10 steps (2 octaves)
    int scaleStep = juce::jlimit(0, 10, static_cast<int>(gyroMag / 50.0f)); 
    int pentatonic[5] = {0, 3, 5, 7, 10};
    int octave = scaleStep / 5;
    int degree = scaleStep % 5;
    float semitones = static_cast<float>(octave * 12 + pentatonic[degree]);

    // C3 = 130.81 Hz
    out.rootHz = 130.81f * std::pow(2.0f, semitones / 12.0f);

    // Voices: Root and 5th
    out.numVoices = 2;
    out.chordSemitones[0] = 0.0f;
    out.chordSemitones[1] = 7.0f;

    // Modulation: Pitch and Yaw
    float pitchNorm = juce::jlimit(0.0f, 1.0f, (params.pitch + 1.57f) / 3.14f);
    float yawNorm = juce::jlimit(0.0f, 1.0f, (params.yaw + 3.14f) / 6.28f);

    out.vibratoDepth = pitchNorm * 0.025f;
    out.vibratoRateHz = 4.0f + accelMag * 0.1f;

    out.panL[0] = 1.0f - yawNorm; out.panR[0] = yawNorm;
    out.panL[1] = yawNorm; out.panR[1] = 1.0f - yawNorm;

    // Master gain linked to motion energy
    float energy = juce::jlimit(0.0f, 1.0f, (gyroMag * 0.01f) + (accelMag * 0.05f));
    out.masterGain = 0.05f + energy * 0.8f;
    out.voiceGain[0] = 0.9f;
    out.voiceGain[1] = 0.7f;

    // Roll: Frequency Cutoff Filter
    float rollNorm = juce::jlimit(0.0f, 1.0f, (params.roll + 3.14f) / 6.28f);
    float maxActivePartial = 1.0f + rollNorm * 5.0f;

    for (int p = 0; p < 6; ++p) {
        float p_idx = static_cast<float>(p + 1);
        if (p_idx <= maxActivePartial) {
            out.partialAmps[p] = 1.0f / p_idx;
        } else if (p_idx - maxActivePartial < 1.0f) {
            out.partialAmps[p] = (1.0f - (p_idx - maxActivePartial)) * (1.0f / p_idx);
        } else {
            out.partialAmps[p] = 0.0f;
        }
    }

    out.noiseAmount = energy * 0.15f;
    out.noiseLpCoef = 0.05f + rollNorm * 0.5f;
    out.driveAmt = energy * 1.5f;
}