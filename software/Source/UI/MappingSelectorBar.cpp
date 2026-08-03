#include "MappingSelectorBar.h"
#include "../PluginProcessor.h"
#include "DebugLog.h"
#include "Palette.h"
#include "StyleHelpers.h"

MappingSelectorBar::MappingSelectorBar(REMORAProcessor& p) : processor(p) {
    mappingCombo.setColour(juce::ComboBox::backgroundColourId, Palette::panel);
    mappingCombo.setColour(juce::ComboBox::textColourId, Palette::textHi);
    mappingCombo.setColour(juce::ComboBox::outlineColourId, Palette::border);
    mappingCombo.setColour(juce::ComboBox::arrowColourId, Palette::textMid);

    const int mappingCount = processor.getSynth().getMappingCount();
    for (int i = 0; i < mappingCount; ++i) {
        const char* name = processor.getSynth().getMappingName(i);
        if (name != nullptr)
            mappingCombo.addItem(name, i + 1);
    }
    mappingCombo.setSelectedId(processor.getMappingStrategy() + 1, juce::dontSendNotification);
    mappingCombo.onChange = [this] {
        const int newStrategyIndex = mappingCombo.getSelectedId() - 1;
        processor.setMappingStrategy(newStrategyIndex);
        debug.print.cyan("Mapping strategy changed to:", processor.getSynth().getMappingName(newStrategyIndex));
        notifyMappingChanged();
    };
    addAndMakeVisible(mappingCombo);

    styleButton(prevMapButton, "<", Palette::ButtonTheme::secondary,
               [this, mappingCount] {
                   const int newStrategyIndex = (mappingCombo.getSelectedId() - 2 + mappingCount) % mappingCount;
                   processor.setMappingStrategy(newStrategyIndex);
                   mappingCombo.setSelectedItemIndex(newStrategyIndex);
                   notifyMappingChanged();
               }
    );
    addAndMakeVisible(prevMapButton);

    styleButton(nextMapButton, ">", Palette::ButtonTheme::secondary,
               [this, mappingCount] {
                   const int newStrategyIndex = mappingCombo.getSelectedId() % mappingCount;
                   processor.setMappingStrategy(newStrategyIndex);
                   mappingCombo.setSelectedItemIndex(newStrategyIndex);
                   notifyMappingChanged();
               }
    );
    addAndMakeVisible(nextMapButton);

    globalVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    globalVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    globalVolumeSlider.setColour(juce::Slider::textBoxTextColourId, Palette::textHi);
    globalVolumeSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    globalVolumeSlider.setRange(0.0, 2.0, 0.01);
    globalVolumeSlider.textFromValueFunction = [](double val) {
        return juce::String(juce::roundToInt(val * 100.0)) + "%";
    };
    globalVolumeSlider.valueFromTextFunction = [](const juce::String& text) {
        return text.upToFirstOccurrenceOf("%", false, false).getDoubleValue() / 100.0;
    };
    globalVolumeSlider.setValue(processor.getSynth().uiGlobalVolume.load());
    globalVolumeSlider.onValueChange = [this] {
        processor.getSynth().uiGlobalVolume.store(static_cast<float>(globalVolumeSlider.getValue()));
    };
    addAndMakeVisible(globalVolumeSlider);

    styleLabel(globalVolumeLabel, "Global Volume", 14.f, Palette::textMid, juce::Justification::centredRight);
    addAndMakeVisible(globalVolumeLabel);

    styleButton(resetButton, "Reset Changes", Palette::ButtonTheme::warning, [this] {
        if (onResetRequested)
            onResetRequested();
    });
    resetButton.setVisible(false);
    addAndMakeVisible(resetButton);

    styleButton(layoutButton, "Auto Layout", Palette::ButtonTheme::secondary, [this] {
        if (onLayoutRequested)
            onLayoutRequested();
    });
    addAndMakeVisible(layoutButton);

    auto setupSepSlider = [this](juce::Slider& slider, juce::Label& label, const juce::String& labelText,
                                  const juce::String& tooltip, float defaultValue,
                                  std::function<void(float)>& callback) {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 20);
        slider.setColour(juce::Slider::textBoxTextColourId, Palette::textHi);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setRange(0.0, 200.0, 1.0);
        slider.setValue(defaultValue, juce::dontSendNotification);
        slider.setTooltip(tooltip);
        slider.onValueChange = [&slider, &callback] {
            if (callback)
                callback(static_cast<float>(slider.getValue()));
        };
        addAndMakeVisible(slider);

        styleLabel(label, labelText, 14.f, Palette::textMid, juce::Justification::centredRight);
        label.setTooltip(tooltip);
        addAndMakeVisible(label);
    };
    setupSepSlider(rankSepSlider, rankSepLabel, "Rank Sep", "Spacing between columns (flow direction)", 40.0f,
                    onRankSepChanged);
    setupSepSlider(nodeSepSlider, nodeSepLabel, "Node Sep", "Spacing between lanes (rows)", 24.0f, onNodeSepChanged);

    startTimerHz(10);
}

void MappingSelectorBar::setResetButtonVisible(bool shouldBeVisible) {
    resetButton.setVisible(shouldBeVisible);
}

MappingSelectorBar::~MappingSelectorBar() {
    stopTimer();
}

void MappingSelectorBar::notifyMappingChanged() {
    if (onMappingChanged)
        onMappingChanged();
}

void MappingSelectorBar::timerCallback() {
    if (!mappingCombo.isPopupActive() && mappingCombo.getSelectedId() != processor.getMappingStrategy() + 1) {
        mappingCombo.setSelectedId(processor.getMappingStrategy() + 1, juce::dontSendNotification);
        notifyMappingChanged();
    }
}

void MappingSelectorBar::resized() {
    auto bounds = getLocalBounds().reduced(10, 4);
    resetButton.setBounds(bounds.removeFromRight(110));
    bounds.removeFromRight(10);
    layoutButton.setBounds(bounds.removeFromRight(100));
    bounds.removeFromRight(10);
    nodeSepSlider.setBounds(bounds.removeFromRight(90));
    nodeSepLabel.setBounds(bounds.removeFromRight(60));
    bounds.removeFromRight(10);
    rankSepSlider.setBounds(bounds.removeFromRight(90));
    rankSepLabel.setBounds(bounds.removeFromRight(60));
    bounds.removeFromRight(10);
    mappingCombo.setBounds(bounds.removeFromLeft(100));
    bounds.removeFromLeft(5);
    prevMapButton.setBounds(bounds.removeFromLeft(25));
    nextMapButton.setBounds(bounds.removeFromLeft(25));
    bounds.removeFromLeft(15);
    globalVolumeLabel.setBounds(bounds.removeFromLeft(100));
    bounds.removeFromLeft(5);
    globalVolumeSlider.setBounds(bounds.removeFromLeft(200));
}
