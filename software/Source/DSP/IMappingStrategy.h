#pragma once

#include "MathHelpers.h"
#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

struct StaffSoundParams;

// Everything the Additive Synth engine needs, written by the single "sink.additiveSynth" mega-node.
struct AdditiveSynthParams {
    float rootHz = 110.0f;
    // All voices sing the same pitch as voice 0 by default (no chord).
    float chordSemitones[3] = { 0.0f, 0.0f, 0.0f };
    int numVoices = 1;

    // Only voice 0 sounds by default; voices 1-3 are silent until a preset opts them in.
    float voiceGain[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

    // Fundamental only by default - a pure sine tone until a preset adds harmonics.
    float partialAmps[6] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float driveAmt = 0.0f;

    float vibratoDepth = 0.0f;
    float vibratoRateHz = 5.0f;
    float tremoloDepth = 0.0f;
    float tremoloRateHz = 4.0f;

    float noiseAmount = 0.0f;
    float noiseLpCoef = 0.5f;
    bool  usePinkNoise = false;

    // Panning disabled by default - equal gain to both channels for every voice.
    float panL[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
    float panR[4] = { 0.5f, 0.5f, 0.5f, 0.5f };

    float lpfCutoffHz = 20000.0f;

    bool  useIndependentVoicePitch = false;
    float voiceHz[4] = { 110.0f, 110.0f, 110.0f, 110.0f };

    float reverbWetLevel = 0.0f;   // 0 = dry/bypassed, 1 = fully wet
    float reverbRoomSize = 0.5f;   // decay length ("feedback"): 0 = short, 1 = long
    float reverbDamping  = 0.5f;   // high-frequency absorption: 0 = bright, 1 = dark
};

// Everything the Granular Synth engine needs, written by the single "sink.granularSynth" mega-node.
// `level` defaults to 0 (silent) so presets that don't wire this node leave the engine inaudible.
struct GranularSynthParams {
    float positionNorm = 0.0f;    // 0-1, scan position in the internal source buffer
    float positionSpray = 0.0f;   // 0-1, random jitter added to position per grain
    float grainSizeMs = 60.0f;    // 5-500
    float densityHz = 20.0f;      // 1-200 grains/sec
    float pitchSemitones = 0.0f;  // -24..24, grain playback pitch
    float pitchSpray = 0.0f;      // 0-12 semitones, random jitter per grain
    float panSpread = 0.0f;       // 0-1, stereo spread of grains
    float ampSpray = 0.0f;        // 0-1, random amplitude jitter per grain
    float level = 0.0f;           // 0-1, engine output trim
    float reverseAmount = 0.0f;   // 0-1, probability a grain plays reversed
};

// Everything the Pink Trombone (vocal tract) engine needs, written by the single
// "sink.pinkTromboneSynth" mega-node. `level` defaults to 0 (silent) so presets that don't wire
// this node leave the engine inaudible.
struct PinkTromboneParams {
    float frequencyHz = 140.0f;         // 40-600, glottal pitch
    float tenseness = 0.6f;             // 0-1, vocal fold tension: breathy (low) to pressed (high)
    float tongueIndexNorm = 0.5f;       // 0-1, front-back tongue position along its allowed range
    float tongueDiameter = 2.75f;       // ~1.5-3.5, tongue height: smaller = narrower/raised, larger = wider/lowered
    float constrictionIndexNorm = 0.5f; // 0-1, position along the tract of an extra constriction
    float constrictionDiameter = 4.0f;  // 0-4, diameter of that constriction; >= ~3.7 is a no-op (open)
    float fricativeIntensity = 0.0f;    // 0-1, turbulence noise injected at the constriction
    float level = 0.0f;                 // 0-1, engine output trim
};

// Written once per audio block by whichever mega-sink nodes the active preset's graph wires up.
// `masterGain` is shared by every engine (written by "sink.generalGain"); each engine param struct
// defaults to silent/off so an engine a preset never wires stays inaudible.
struct MappingOutput {
    float masterGain = 0.0f;
    AdditiveSynthParams additive;
    GranularSynthParams granular;
    PinkTromboneParams pinkTrombone;
    bool additiveActive = false;
    bool granularActive = false;
    bool pinkTromboneActive = false;
};

class IMappingStrategy {
public:
    struct MonitorParam {
        juce::String name;
        juce::String driveInfo;
        float rangeMin;
        float rangeMax;
        std::atomic<float> value;
        // Empty = numeric meter. Non-empty = categorical: `value` (rounded) indexes this list of labels.
        juce::StringArray textLabels;

        MonitorParam(juce::String paramName, juce::String paramDriveInfo, float paramRangeMin, float paramRangeMax)
            : name(std::move(paramName)), driveInfo(std::move(paramDriveInfo)),
              rangeMin(paramRangeMin), rangeMax(paramRangeMax), value(paramRangeMin) {}
    };

    virtual ~IMappingStrategy() = default;
    virtual void prepare(double sampleRate) { (void)sampleRate; }
    virtual bool process(const StaffSoundParams& in, MappingOutput& out) = 0;
    virtual const char* getName() const = 0;
    // What the preset does/sounds like, shown to the performer picking a mapping. Empty if not written yet.
    virtual const juce::String& getDescription() const { static const juce::String empty; return empty; }

    int getMonitorParamCount() const noexcept { return static_cast<int>(monitorParams.size()); }
    const MonitorParam& getMonitorParam(int index) const { return *monitorParams[static_cast<size_t>(index)]; }

protected:
    MonitorParam& addMonitorParam(juce::String name, juce::String driveInfo, float rangeMin = 0.0f, float rangeMax = 1.0f) {
        monitorParams.push_back(std::make_unique<MonitorParam>(std::move(name), std::move(driveInfo), rangeMin, rangeMax));
        return *monitorParams.back();
    }

    // For discrete states (e.g. "CW"/"CCW") - write the label's index to `.value` from process().
    MonitorParam& addTextMonitorParam(juce::String name, juce::String driveInfo, juce::StringArray textLabels) {
        auto& param = addMonitorParam(std::move(name), std::move(driveInfo), 0.0f,
                                       static_cast<float>(juce::jmax(1, textLabels.size() - 1)));
        param.textLabels = std::move(textLabels);
        return param;
    }

private:
    std::vector<std::unique_ptr<MonitorParam>> monitorParams;
};