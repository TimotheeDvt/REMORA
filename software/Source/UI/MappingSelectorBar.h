#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <unordered_map>

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
    std::function<void()> onPresetSaved;
    void setResetButtonVisible(bool shouldBeVisible);

private:
    REMORAProcessor& processor;

    juce::ComboBox mappingCombo;
    juce::TextButton prevMapButton;
    juce::TextButton nextMapButton;
    juce::TextButton newPresetButton;
    juce::TextButton savePresetButton;
    juce::TextButton loadPresetButton;
    juce::TextButton optionsButton;
    juce::TextButton resetButton;
    juce::TextButton layoutButton;
    juce::Slider globalVolumeSlider;
    juce::Label globalVolumeLabel;
    juce::Slider rankSepSlider;
    juce::Label rankSepLabel;
    juce::Slider nodeSepSlider;
    juce::Label nodeSepLabel;

    std::unordered_map<int, juce::File> presetFileByMappingIndex;

    std::unique_ptr<juce::AlertWindow> namePromptWindow;

    void timerCallback() override;
    void notifyMappingChanged();
    void refreshMappingCombo(int selectIndex);
    void selectMapping(int index);

    void promptForPresetName(const juce::String& title, const juce::String& initialValue,
                              std::function<void(juce::String)> onConfirmed);
    void createNewPreset();
    void saveCurrentPreset();
    void saveCurrentPresetAs();
    void showLoadPresetMenu();
    void showOptionsMenu();
    void changePresetFolder();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MappingSelectorBar)
};
