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

    void timerCallback() override;
    void notifyMappingChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MappingSelectorBar)
};
