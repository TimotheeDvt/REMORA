#include "SpinFilterMapping.h"
#include "../BoStaffSynth.h"

void SpinFilterMapping::process(const StaffSoundParams &params, MappingOutput &out) {
    // 1. PITCH: Derived from combination of motion (Gyroscope Magnitude)
    // Instead of using direct pitch/yaw which wrap around wildly when spinning,
    // we use the speed of rotation to climb a C minor pentatonic scale.
    // Spinning faster holds higher notes securely without cascading chaos.
    float gyroMag = std::sqrt(params.gx * params.gx + params.gy * params.gy + params.gz * params.gz);
    float accelMag = std::sqrt(params.ax * params.ax + params.ay * params.ay + params.az * params.az);

    // Map gyro magnitude to a scale step. Decreased sensitivity to make note distances bigger,
    // and clamped to 10 steps (exactly 2 octaves in a pentatonic scale).
    int scaleStep = juce::jlimit(0, 10, static_cast<int>(gyroMag / 50.0f)); 
    int pentatonic[5] = {0, 3, 5, 7, 10}; // Minor pentatonic intervals
    int octave = scaleStep / 5;
    int degree = scaleStep % 5;
    float semitones = static_cast<float>(octave * 12 + pentatonic[degree]);

    // Base note C3 = 130.81 Hz
    out.rootHz = 130.81f * std::pow(2.0f, semitones / 12.0f);

    // Use 2 thick voices (Root and Perfect 5th)
    out.numVoices = 2;
    out.chordSemitones[0] = 0.0f;
    out.chordSemitones[1] = 7.0f;

    // 2. MODULATION: Repurpose Pitch and Yaw
    // Normalize Euler angles to 0..1 range
    float pitchNorm = juce::jlimit(0.0f, 1.0f, (params.pitch + 1.57f) / 3.14f);
    float yawNorm = juce::jlimit(0.0f, 1.0f, (params.yaw + 3.14f) / 6.28f);

    out.vibratoDepth = pitchNorm * 0.025f;       // Tilt up/down changes vibrato depth
    out.vibratoRateHz = 4.0f + accelMag * 0.1f;

    out.panL[0] = 1.0f - yawNorm; out.panR[0] = yawNorm; // Horizontal pointing sets panning
    out.panL[1] = yawNorm; out.panR[1] = 1.0f - yawNorm; // 2nd voice pans oppositely for width

    // 3. MASTER GAIN: Linked to general motion energy
    float energy = juce::jlimit(0.0f, 1.0f, (gyroMag * 0.01f) + (accelMag * 0.05f));
    out.masterGain = 0.05f + energy * 0.8f;
    out.voiceGain[0] = 0.9f;
    out.voiceGain[1] = 0.7f;

    // 4. ROLL: Frequency Cutoff Filter implementation
    // Roll maps to upper harmonics fading out smoothly, mimicking a Low Pass Filter
    float rollNorm = juce::jlimit(0.0f, 1.0f, (params.roll + 3.14f) / 6.28f);
    float maxActivePartial = 1.0f + rollNorm * 5.0f; // Range: 1.0 to 6.0

    for (int p = 0; p < 6; ++p) {
        float p_idx = static_cast<float>(p + 1);
        if (p_idx <= maxActivePartial) {
            out.partialAmps[p] = 1.0f / p_idx; // Natural harmonic falloff
        } else if (p_idx - maxActivePartial < 1.0f) {
            out.partialAmps[p] = (1.0f - (p_idx - maxActivePartial)) * (1.0f / p_idx); // Smooth fade
        } else {
            out.partialAmps[p] = 0.0f; // Filtered out
        }
    }

    out.noiseAmount = energy * 0.15f;
    out.noiseLpCoef = 0.05f + rollNorm * 0.5f; // Actual lowpass on the noise channel
    out.driveAmt = energy * 1.5f;
}