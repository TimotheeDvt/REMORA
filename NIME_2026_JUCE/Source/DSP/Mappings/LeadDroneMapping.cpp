#include "LeadDroneMapping.h"
#include "../BoStaffSynth.h"

void LeadDroneMapping::process(const StaffSoundParams &params, MappingOutput &out) {
    // Map Pitch (tilt up/down) to a major scale.
    // A major scale has 7 notes. A multiplier of 7 gives a nice feel for stepping through the scale
    // over a ~3 octave range.
    int scaleStep = static_cast<int>(std::round(params.pitch * 7.0f));

    // A Major scale intervals
    int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };

    int octave = scaleStep / 7;
    int degree = scaleStep % 7;
    if (degree < 0) {
        degree += 7;
        octave -= 1;
    }

    // Root note A3 = 220Hz
    float semitones = static_cast<float>(octave * 12 + majorScale[degree]);
    out.rootHz = 220.0f * std::pow(2.0f, semitones / 12.0f);

    // Lead Voice + 3 Drone Voices
    out.numVoices = 4;

    // Drone notes provide an automatic bass drone.
    // We want the drone to be fixed at A2, A1, and E2, regardless of the lead's
    // pitch. Since voice 1, 2, 3 frequencies are calculated as: rootHz *
    // 2^(chordSemitones[i]/12), we offset them by `-semitones` to counteract
    // the lead's root transposition.
    out.chordSemitones[0] = -12.0f - semitones; // Drone 1 (A2)
    out.chordSemitones[1] = -24.0f - semitones; // Drone 2 (A1)
    out.chordSemitones[2] = -19.0f - semitones; // Drone 3 (E2)

    // Evolve characteristics based on staff acceleration and position
    float accelMag = std::sqrt(params.ax * params.ax + params.ay * params.ay +
                               params.az * params.az);
    float gyroMag = std::sqrt(params.gx * params.gx + params.gy * params.gy +
                              params.gz * params.gz);

    // Yaw (-180 to 180) changes drone timbre/pan balance
    float yawNorm = juce::jlimit(0.0f, 1.0f, (params.yaw + 180.0f) / 360.0f);

    // Master gain has a high baseline so the drone is *always* on
    out.masterGain = 0.8f + juce::jlimit(0.0f, 0.2f, gyroMag * 0.01f);

    // Lead Voice Gain
    out.voiceGain[0] = 0.6f + 0.4f * juce::jlimit(0.0f, 1.0f, accelMag * 0.1f);

    // Drone Voices Gain (always strong, panning via yaw)
    float droneSwell = 1.2f + 0.4f * juce::jlimit(0.0f, 1.0f, accelMag * 0.15f);
    out.voiceGain[1] = 1.0f * droneSwell * (1.0f - yawNorm);
    out.voiceGain[2] = 1.0f * droneSwell * yawNorm;
    out.voiceGain[3] = 0.8f * droneSwell;

    // Timbre evolving with acceleration
    out.driveAmt = juce::jlimit(0.0f, 2.5f, accelMag * 0.2f);
    out.noiseAmount = juce::jlimit(0.0f, 0.3f, accelMag * 0.05f);
    out.noiseLpCoef = 0.2f;

    // Vibrato and Tremolo evolving with position and motion
    out.vibratoDepth =
        juce::jlimit(0.0f, 0.02f, std::abs(params.pitch) * 0.0005f);
    out.vibratoRateHz = 4.0f + gyroMag * 0.05f;
    out.tremoloDepth = juce::jlimit(0.0f, 0.3f, std::abs(params.yaw) * 0.002f);
    out.tremoloRateHz = 2.0f + accelMag * 0.1f;

    // Partials brightness evolving with acceleration
    float brightness = juce::jlimit(0.0f, 1.0f, accelMag * 0.15f);
    out.partialAmps[0] = 1.0f;
    out.partialAmps[1] = 0.7f * brightness;
    out.partialAmps[2] = 0.5f * brightness;
    out.partialAmps[3] = 0.3f * brightness;
    out.partialAmps[4] = 0.2f * brightness;
    out.partialAmps[5] = 0.1f * brightness;

    // Stereo spread
    out.panL[0] = 0.6f; out.panR[0] = 0.6f; // Lead center
    out.panL[1] = 0.9f; out.panR[1] = 0.1f; // Drone 1 left
    out.panL[2] = 0.1f; out.panR[2] = 0.9f; // Drone 2 right
    out.panL[3] = 0.5f; out.panR[3] = 0.5f; // Drone 3 center
}