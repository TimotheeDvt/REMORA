#pragma once

#include <JuceHeader.h>

// One synthesis engine (additive, granular, ...) owned by SynthManager. Each engine renders its
// own params struct (a field of MappingOutput) into its own scratch buffer every block; SynthManager
// sums every engine's scratch buffer and applies shared gain/mute on top.
class ISynthEngine {
public:
    virtual ~ISynthEngine() = default;

    virtual void prepare(double sampleRate, int samplesPerBlock) = 0;

    // Renders exactly `numSamples` samples into `buffer` (already sized/cleared by the caller).
    virtual void render(juce::AudioBuffer<float>& buffer, int numSamples) = 0;
};
