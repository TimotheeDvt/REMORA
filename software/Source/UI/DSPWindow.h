#pragma once

#include "SpectrumAnalyserThread.h"
#include <JuceHeader.h>

class REMORAProcessor;
class IMappingStrategy;

// If textLabels is non-empty, the knob is replaced by a plain text readout:
// setValue() rounds to the nearest index and shows that label (e.g. "CW").
class MonitorKnobComponent : public juce::Component {
public:
    MonitorKnobComponent(const juce::String& name, const juce::String& driveInfo, float rangeMin, float rangeMax,
                          juce::StringArray textLabels = {});

    void resized() override;
    void setValue(float newValue);

private:
    juce::Label nameLabel;
    juce::Slider knob;
    juce::Label textValueLabel;
    juce::Label driveInfoLabel;
    juce::StringArray textLabels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonitorKnobComponent)
};

class DSPComponent : public juce::Component, private juce::Timer {
public:
    explicit DSPComponent(REMORAProcessor& p);
    ~DSPComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    REMORAProcessor& processor;

    juce::ComboBox mappingCombo;
    juce::TextButton prevMapButton;
    juce::TextButton nextMapButton;
    juce::Slider globalVolumeSlider;
    juce::Label globalVolumeLabel;
    juce::Label rootNoteLabel;

    SpectrumAnalyserThread spectrumAnalyser;
    std::array<float, SpectrumAnalyserThread::numBins> spectrumDb {};
    juce::Path spectrumPath;

    juce::OwnedArray<MonitorKnobComponent> monitorKnobs;
    const IMappingStrategy* monitoredMapping = nullptr;
    int scopeTopInset = 40;

    void rebuildMonitorKnobs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSPComponent)
};

class DSPWindow : public juce::DocumentWindow {
public:
    explicit DSPWindow(REMORAProcessor& p);
    void closeButtonPressed() override;

private:
    DSPComponent dspComponent;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSPWindow)
};