#pragma once

#include "MathHelpers.h"
#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

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

    float lpfCutoffHz = 20000.0f;

    bool  useIndependentVoicePitch = false;
    float voiceHz[4] = { 110.0f, 110.0f, 110.0f, 110.0f };

    float reverbWetLevel = 0.0f;   // 0 = dry/bypassed, 1 = fully wet
    float reverbRoomSize = 0.5f;   // decay length ("feedback"): 0 = short, 1 = long
    float reverbDamping  = 0.5f;   // high-frequency absorption: 0 = bright, 1 = dark
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
    virtual void process(const StaffSoundParams& in, MappingOutput& out) = 0;
    virtual const char* getName() const = 0;

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