#pragma once

#include "MathHelpers.h"
#include <JuceHeader.h>

struct StaffSoundParams;

struct MappingOutput {
    float rootHz = 110.0f;
    float chordSemitones[3] = { 7.0f, 12.0f, 19.0f };
    int numVoices = 4;

    float masterGain = 0.0f;
    float voiceGain[4] = { 1.0f, 0.75f, 0.75f, 0.9f };

    float partialAmps[6] = { 1.0f, 0.5f, 0.25f, 0.1f, 0.05f, 0.02f };
    float driveAmt = 0.0f;

    float vibratoDepth = 0.0f;
    float vibratoRateHz = 5.0f;
    float tremoloDepth = 0.0f;
    float tremoloRateHz = 4.0f;

    float noiseAmount = 0.0f;
    float noiseLpCoef = 0.5f;

    float panL[4] = { 0.85f, 0.55f, 0.45f, 0.15f };
    float panR[4] = { 0.15f, 0.45f, 0.55f, 0.85f };
};

class IMappingStrategy {
public:
    virtual ~IMappingStrategy() = default;
    virtual void prepare(double sampleRate) { (void)sampleRate; }
    virtual void process(const StaffSoundParams& in, MappingOutput& out) = 0;
    virtual const char* getName() const = 0;
};