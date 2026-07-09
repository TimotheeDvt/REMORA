#include "DSPWindow.h"
#include "../PluginProcessor.h"
#include "Palette.h"
#include "StyleHelpers.h"
#include "DebugLog.h"

DSPComponent::DSPComponent(NIMEReceiverProcessor& p)
    : processor(p),
      forwardFFT(BoStaffSynth::fftOrder),
      window(BoStaffSynth::fftSize, juce::dsp::WindowingFunction<float>::hann)
{
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
        int newStrategyIndex = mappingCombo.getSelectedId() - 1;
        processor.setMappingStrategy(newStrategyIndex);
        debug.print.cyan("Mapping strategy changed to:", processor.getSynth().getMappingName(newStrategyIndex));
    };
    addAndMakeVisible(mappingCombo);

    prevMapButton.setButtonText("<");
    prevMapButton.setColour(juce::TextButton::buttonColourId, Palette::panel);
    prevMapButton.setColour(juce::TextButton::textColourOffId, Palette::textMid);
    prevMapButton.onClick = [this, mappingCount] {
        int newStrategyIndex = (mappingCombo.getSelectedId() - 2 + mappingCount) % mappingCount;
        debug.print.cyan(newStrategyIndex);
        processor.setMappingStrategy(newStrategyIndex);
        mappingCombo.setSelectedItemIndex(newStrategyIndex);
    };
    addAndMakeVisible(prevMapButton);

    nextMapButton.setButtonText(">");
    nextMapButton.setColour(juce::TextButton::buttonColourId, Palette::panel);
    nextMapButton.setColour(juce::TextButton::textColourOffId, Palette::textMid);
    nextMapButton.onClick = [this, mappingCount] {
        int newStrategyIndex = (mappingCombo.getSelectedId()) % mappingCount;
        processor.setMappingStrategy(newStrategyIndex);
        mappingCombo.setSelectedItemIndex(newStrategyIndex);
    };
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

    styleLabel(rootNoteLabel, "Root Freq: -- Hz", 14.f, Palette::textHi, juce::Justification::centredRight);
    addAndMakeVisible(rootNoteLabel);

    startTimerHz(30);
}

DSPComponent::~DSPComponent() {
    stopTimer();
}

void DSPComponent::timerCallback() {
    auto& synth = processor.getSynth();

    if (synth.nextFFTBlockReady.load()) {
        std::copy(synth.fftData.begin(), synth.fftData.begin() + BoStaffSynth::fftSize, fftData.begin());
        std::fill(fftData.begin() + BoStaffSynth::fftSize, fftData.end(), 0.0f);
        synth.nextFFTBlockReady.store(false);

        window.multiplyWithWindowingTable(fftData.data(), BoStaffSynth::fftSize);
        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

        auto scopeBounds = getLocalBounds().withTrimmedTop(40).reduced(10).toFloat();
        spectrumPath.clear();

        const float minDB = -100.0f;
        const float maxDB = 0.0f;

        for (size_t i = 0; i < static_cast<size_t>(BoStaffSynth::fftSize / 2); ++i) {
            float level = juce::Decibels::gainToDecibels(fftData[i]) - juce::Decibels::gainToDecibels((float)BoStaffSynth::fftSize);
            level = juce::jlimit(minDB, maxDB, level);

            float x = juce::jmap((float)i, 0.0f, (float)(BoStaffSynth::fftSize / 2), scopeBounds.getX(), scopeBounds.getRight());
            float y = juce::jmap(level, minDB, maxDB, scopeBounds.getBottom(), scopeBounds.getY());

            if (i == 0) spectrumPath.startNewSubPath(x, y);
            else spectrumPath.lineTo(x, y);
        }

        repaint(scopeBounds.toNearestInt());
    }

    rootNoteLabel.setText("Root Freq: " + juce::String(synth.getCurrentRootFreq(), 1) + " Hz", juce::dontSendNotification);

    if (!mappingCombo.isPopupActive() && mappingCombo.getSelectedId() != processor.getMappingStrategy() + 1) {
        mappingCombo.setSelectedId(processor.getMappingStrategy() + 1, juce::dontSendNotification);
    }
}

void DSPComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::bg);

    auto scopeBounds = getLocalBounds().withTrimmedTop(40).reduced(10);
    g.setColour(Palette::panel);
    g.fillRect(scopeBounds);

    g.setColour(Palette::accentDim);
    g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));

    float maxHz = processor.getSynth().getSampleRate() / 2.0f;
    if (maxHz > 0.0f) {
        float lpfHz = processor.getSynth().getCurrentLpfCutoff();
        float lpfX = juce::jmap(lpfHz, 0.0f, maxHz, (float)scopeBounds.getX(), (float)scopeBounds.getRight());

        g.setColour(Palette::yellow.withAlpha(0.8f));
        g.drawVerticalLine(static_cast<int>(lpfX), static_cast<float>(scopeBounds.getY()), static_cast<float>(scopeBounds.getBottom()));
        g.setFont(12.0f);
        g.drawText("LPF " + juce::String(lpfHz, 0) + "Hz", static_cast<int>(lpfX) - 60, scopeBounds.getY() + 5, 55, 20, juce::Justification::centredRight, false);
    }
}

void DSPComponent::resized() {
    auto bounds = getLocalBounds().reduced(10);
    auto row = bounds.removeFromTop(24);

    mappingCombo.setBounds(row.removeFromLeft(100));
    row.removeFromLeft(5);
    prevMapButton.setBounds(row.removeFromLeft(25));
    nextMapButton.setBounds(row.removeFromLeft(25));
    globalVolumeLabel.setBounds(row.removeFromLeft(100));
    row.removeFromLeft(5);
    globalVolumeSlider.setBounds(row.removeFromLeft(200));

    rootNoteLabel.setBounds(row.removeFromRight(150));
}

DSPWindow::DSPWindow(NIMEReceiverProcessor& p)
    : juce::DocumentWindow("DSP Panel", Palette::bg, juce::DocumentWindow::allButtons),
      dspComponent(p)
{
    setContentNonOwned(&dspComponent, true);
    setResizable(true, true);
    setResizeLimits(400, 300, 2000, 2000);
    setSize(600, 400);
    setUsingNativeTitleBar(true);
}

void DSPWindow::closeButtonPressed() {
    setVisible(false);
}