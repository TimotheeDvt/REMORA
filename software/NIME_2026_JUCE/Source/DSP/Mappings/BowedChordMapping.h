#pragma once
#include "../IMappingStrategy.h"
#include <JuceHeader.h>
#include <cstdint>
#include <cmath>

class BowedChordMapping : public IMappingStrategy {
public:
    const char* getName() const override { return "Bowed Chord"; }
    void prepare(double sampleRate) override;
    void process(const StaffSoundParams& in, MappingOutput& out) override;

private:
    float prevAccelMag  = 0.0f;
    float noiseEnvelope = 0.0f;
    double sampleRate_  = 44100.0;

    static float pitchToRootHz(float pitchRad);
    static int   yawToChordIdx(float yawRad, float rollAbs);

    static constexpr int kNumChords = 6;
    static constexpr float kChordTable[kNumChords][3] = {
        {  3.f,  7.f, 12.f },
        {  7.f, 12.f, 19.f },
        {  4.f,  7.f, 12.f },
        {  5.f,  7.f, 12.f },
        {  3.f,  7.f, 10.f },
        {  4.f,  7.f, 11.f },
    };
};