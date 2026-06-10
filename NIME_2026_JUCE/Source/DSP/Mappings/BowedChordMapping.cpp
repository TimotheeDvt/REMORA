#include "BowedChordMapping.h"
#include "../BoStaffSynth.h"   // for StaffSoundParams
#include "../../UI/DebugLog.h"
#include <cmath>
#include <algorithm>

constexpr float BowedChordMapping::kChordTable[kNumChords][3];

void BowedChordMapping::prepare(double sampleRate) {
    debug.print.green("BowedChordMapping prepared at sample rate:", sampleRate);
    sampleRate_  = sampleRate;
    prevAccelMag = 0.0f;
    noiseEnvelope = 0.0f;
}

float BowedChordMapping::pitchToRootHz(float pitchRad) {
    constexpr float pi = 3.14159265f;
    float norm     = juce::jlimit(0.0f, 1.0f, (pitchRad + pi * 0.5f) / pi);
    float midiFloat = 36.0f + norm * 24.0f;
    int midiNote   = static_cast<int>(std::round(midiFloat));
    return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
}

int BowedChordMapping::yawToChordIdx(float yawRad, float rollAbs) {
    constexpr float pi = 3.14159265f;
    float yawNorm = juce::jlimit(0.0f, 1.0f, (yawRad + pi) / (2.0f * pi));
    if (rollAbs > 0.7f)
        return yawNorm < 0.5f ? 4 : 5;
    if (yawNorm < 0.25f) return 0;
    if (yawNorm < 0.50f) return 1;
    if (yawNorm < 0.75f) return 2;
    return 3;
}

void BowedChordMapping::process(const StaffSoundParams& in, MappingOutput& out) {
    constexpr float pi = 3.14159265f;

    out.rootHz = pitchToRootHz(in.pitch);

    float rollAbs = juce::jlimit(0.0f, 1.0f, std::abs(in.roll) / pi);
    int chordIdx  = yawToChordIdx(in.yaw, rollAbs);
    out.numVoices = 4;
    for (int v = 0; v < 3; ++v)
        out.chordSemitones[v] = kChordTable[chordIdx][v];

    float gyroMag = std::sqrt(in.gx*in.gx + in.gy*in.gy + in.gz*in.gz);
    constexpr float kBowThresh = 12.0f, kBowSat = 150.0f;
    float bow = (gyroMag < kBowThresh) ? 0.0f
                : juce::jlimit(0.0f, 1.0f, (gyroMag - kBowThresh) / (kBowSat - kBowThresh));

    out.masterGain = 0.05f + bow * 0.20f;
    out.driveAmt   = bow * 1.8f;

    out.voiceGain[0] = bow * 0.90f;
    out.voiceGain[1] = bow * 0.70f;
    out.voiceGain[2] = bow * 0.70f;
    out.voiceGain[3] = bow * 0.90f;

    out.partialAmps[0] = bow;
    out.partialAmps[1] = bow * (0.3f + rollAbs * 0.4f);
    out.partialAmps[2] = bow * 0.35f;
    float accelMag = std::sqrt(in.ax*in.ax + in.ay*in.ay + in.az*in.az);
    out.partialAmps[3] = bow * juce::jlimit(0.0f, 0.5f, accelMag * 0.15f);

    float strikeBoost = juce::jlimit(0.0f, 0.6f, noiseEnvelope * 1.2f);
    out.partialAmps[4] = strikeBoost * 0.4f;
    out.partialAmps[5] = strikeBoost * 0.25f;

    out.vibratoDepth  = rollAbs * 0.022f;
    out.vibratoRateHz = 4.5f + std::abs(in.gz) * 0.03f;

    out.tremoloDepth  = juce::jlimit(0.0f, 0.35f, std::abs(in.gz) / 90.0f);
    out.tremoloRateHz = 3.0f + std::abs(in.gz) * 0.05f;

    float accelDelta = accelMag - prevAccelMag;
    prevAccelMag = accelMag;
    constexpr float kStrikeThresh = 3.0f;
    if (accelDelta > kStrikeThresh)
        noiseEnvelope = juce::jlimit(0.0f, 1.0f,
                        noiseEnvelope + (accelDelta - kStrikeThresh) * 0.5f);
    noiseEnvelope *= 0.9990f;

    out.noiseAmount = noiseEnvelope;
    out.noiseLpCoef = 1.0f - juce::jlimit(0.05f, 0.90f, std::abs(in.az) * 0.18f);

    out.panL[0] = 0.85f; out.panR[0] = 0.15f;
    out.panL[1] = 0.55f; out.panR[1] = 0.45f;
    out.panL[2] = 0.45f; out.panR[2] = 0.55f;
    out.panL[3] = 0.15f; out.panR[3] = 0.85f;
}