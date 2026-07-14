#pragma once

#include "SpectrumAnalyserThread.h"
#include <JuceHeader.h>

class NIMEReceiverProcessor;

class DSPComponent : public juce::Component, private juce::Timer {
public:
    explicit DSPComponent(NIMEReceiverProcessor& p);
    ~DSPComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    NIMEReceiverProcessor& processor;

    juce::ComboBox mappingCombo;
    juce::TextButton prevMapButton;
    juce::TextButton nextMapButton;
    juce::Slider globalVolumeSlider;
    juce::Label globalVolumeLabel;
    juce::Label rootNoteLabel;

    SpectrumAnalyserThread spectrumAnalyser;
    std::array<float, SpectrumAnalyserThread::numBins> spectrumDb {};
    juce::Path spectrumPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSPComponent)
};

class DSPWindow : public juce::DocumentWindow {
public:
    explicit DSPWindow(NIMEReceiverProcessor& p);
    void closeButtonPressed() override;

private:
    DSPComponent dspComponent;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSPWindow)
};