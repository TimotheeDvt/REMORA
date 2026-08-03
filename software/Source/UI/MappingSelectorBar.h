#pragma once

#include <JuceHeader.h>
#include <functional>

class REMORAProcessor;

class MappingSelectorBar : public juce::Component, private juce::Timer {
public:
    explicit MappingSelectorBar(REMORAProcessor& p);
    ~MappingSelectorBar() override;

    void resized() override;

    std::function<void()> onMappingChanged;
    std::function<void()> onResetRequested;
    std::function<void()> onLayoutRequested;
    std::function<void(float)> onRankSepChanged;
    std::function<void(float)> onNodeSepChanged;
    void setResetButtonVisible(bool shouldBeVisible);

private:
    REMORAProcessor& processor;

    juce::ComboBox mappingCombo;
    juce::TextButton prevMapButton;
    juce::TextButton nextMapButton;
    juce::TextButton resetButton;
    juce::TextButton layoutButton;
    juce::Slider globalVolumeSlider;
    juce::Label globalVolumeLabel;
    juce::Slider rankSepSlider;
    juce::Label rankSepLabel;
    juce::Slider nodeSepSlider;
    juce::Label nodeSepLabel;

    void timerCallback() override;
    void notifyMappingChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MappingSelectorBar)
};
