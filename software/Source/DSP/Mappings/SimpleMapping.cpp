#include "SimpleMapping.h"
#include "../BoStaffSynth.h"
#include "../../UI/DebugLog.h"
#include <cmath>

void SimpleMapping::prepare(double sampleRate)
{
    debug.print.green("SimpleMapping prepared at sample rate:", sampleRate);
}

void SimpleMapping::process(const StaffSoundParams& in, MappingOutput& out) {
    constexpr float pi = 3.14159265f;

    out.numVoices = 1;
    out.chordSemitones[0] = 0.f;
    out.chordSemitones[1] = 0.f;
    out.chordSemitones[2] = 0.f;

    float pitchNorm = juce::jlimit(0.0f, 1.0f, (in.pitch + pi * 0.5f) / pi);
    out.rootHz = 100.0f + pitchNorm * 900.0f;

    float rollNorm = juce::jlimit(0.0f, 1.0f, std::abs(in.roll) / pi);
    out.masterGain  = 0.05f + rollNorm * 0.15f;
    out.voiceGain[0] = 1.0f;
    out.voiceGain[1] = 0.0f;
    out.voiceGain[2] = 0.0f;
    out.voiceGain[3] = 0.0f;

    out.partialAmps[0] = 1.0f;
    out.partialAmps[1] = 0.0f;
    out.partialAmps[2] = 0.0f;
    out.partialAmps[3] = 0.0f;
    out.partialAmps[4] = 0.0f;
    out.partialAmps[5] = 0.0f;

    out.driveAmt      = 0.0f;
    out.vibratoDepth  = 0.0f;
    out.vibratoRateHz = 5.0f;
    out.tremoloDepth  = 0.0f;
    out.tremoloRateHz = 4.0f;
    out.noiseAmount   = 0.0f;
    out.noiseLpCoef   = 0.5f;

    out.panL[0] = 0.5f; out.panR[0] = 0.5f;
    out.panL[1] = 0.5f; out.panR[1] = 0.5f;
    out.panL[2] = 0.5f; out.panR[2] = 0.5f;
    out.panL[3] = 0.5f; out.panR[3] = 0.5f;
}