#include "DSPWindow.h"
#include "../PluginProcessor.h"
#include "../DSP/IMappingStrategy.h"
#include "Palette.h"
#include "StyleHelpers.h"
#include "DebugLog.h"
#include <cmath>

namespace {
    // Spectrum display range: below 20Hz is inaudible, and log(0) is
    // undefined, so DC/sub-audio bins are clamped up to this floor.
    constexpr float kSpectrumMinHz = 20.0f;

    float freqToLogX(float hz, float minHz, float maxHz, float left, float right) {
        hz = juce::jlimit(minHz, maxHz, hz);
        return juce::jmap(std::log10(hz), std::log10(minHz), std::log10(maxHz), left, right);
    }
}

MonitorKnobComponent::MonitorKnobComponent(const juce::String& name, const juce::String& driveInfo, float rangeMin, float rangeMax,
                                            juce::StringArray textLabelsIn)
    : textLabels(std::move(textLabelsIn))
{
    styleLabel(nameLabel, name, 11.5f, Palette::textHi, juce::Justification::centred);
    nameLabel.setMinimumHorizontalScale(0.6f);
    addAndMakeVisible(nameLabel);

    if (textLabels.isEmpty()) {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 76, 16);
        knob.setRange(rangeMin, rangeMax, 0.0);
        knob.setNumDecimalPlacesToDisplay(2);
        // Wide ranges (e.g. a 20Hz-20kHz filter cutoff) read better on a log-ish curve.
        if (rangeMin > 0.0f && rangeMax / rangeMin > 50.0f)
            knob.setSkewFactorFromMidPoint(std::sqrt(rangeMin * rangeMax));
        knob.setInterceptsMouseClicks(false, false); // meter only, not user-editable
        knob.setColour(juce::Slider::rotarySliderFillColourId, Palette::accent);
        knob.setColour(juce::Slider::rotarySliderOutlineColourId, Palette::border);
        knob.setColour(juce::Slider::thumbColourId, Palette::accent);
        knob.setColour(juce::Slider::textBoxTextColourId, Palette::textHi);
        knob.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        knob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(knob);
    } else {
        styleLabel(textValueLabel, textLabels[0], 16.0f, Palette::accent, juce::Justification::centred);
        textValueLabel.setColour(juce::Label::outlineColourId, Palette::border);
        addAndMakeVisible(textValueLabel);
    }

    styleLabel(driveInfoLabel, driveInfo, 11.f, Palette::textMid, juce::Justification::centredTop);
    driveInfoLabel.setMinimumHorizontalScale(0.6f);
    addAndMakeVisible(driveInfoLabel);
}

void MonitorKnobComponent::resized() {
    auto bounds = getLocalBounds();
    nameLabel.setBounds(bounds.removeFromTop(16));
    driveInfoLabel.setBounds(bounds.removeFromBottom(44));
    if (textLabels.isEmpty())
        knob.setBounds(bounds);
    else
        textValueLabel.setBounds(bounds.reduced(0, 8));
}

void MonitorKnobComponent::setValue(float newValue) {
    if (textLabels.isEmpty()) {
        knob.setValue(newValue, juce::dontSendNotification);
    } else {
        const int index = juce::jlimit(0, textLabels.size() - 1, juce::roundToInt(newValue));
        textValueLabel.setText(textLabels[index], juce::dontSendNotification);
    }
}

DSPComponent::DSPComponent(REMORAProcessor& p)
    : processor(p),
      spectrumAnalyser(p.getSynth())
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
        rebuildMonitorKnobs();
    };
    addAndMakeVisible(mappingCombo);

    styleButton(prevMapButton, "<", Palette::ButtonTheme::secondary,
               [this, mappingCount] {
                   int newStrategyIndex = (mappingCombo.getSelectedId() - 2 + mappingCount) % mappingCount;
                   debug.print.cyan(newStrategyIndex);
                   processor.setMappingStrategy(newStrategyIndex);
                   mappingCombo.setSelectedItemIndex(newStrategyIndex);
                   rebuildMonitorKnobs();
               }
    );
    addAndMakeVisible(prevMapButton);

    styleButton(nextMapButton, ">", Palette::ButtonTheme::secondary,
               [this, mappingCount] {
                   int newStrategyIndex = (mappingCombo.getSelectedId()) % mappingCount;
                   processor.setMappingStrategy(newStrategyIndex);
                   mappingCombo.setSelectedItemIndex(newStrategyIndex);
                   rebuildMonitorKnobs();
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

    styleLabel(rootNoteLabel, "Root Freq: -- Hz", 14.f, Palette::textHi, juce::Justification::centredRight);
    addAndMakeVisible(rootNoteLabel);

    rebuildMonitorKnobs();

    startTimerHz(30);
}

DSPComponent::~DSPComponent() {
    stopTimer();
}

void DSPComponent::rebuildMonitorKnobs() {
    monitoredMapping = processor.getSynth().getMapping(processor.getMappingStrategy());

    monitorKnobs.clear();
    if (monitoredMapping != nullptr) {
        for (int i = 0; i < monitoredMapping->getMonitorParamCount(); ++i) {
            const auto& param = monitoredMapping->getMonitorParam(i);
            auto* knob = monitorKnobs.add(new MonitorKnobComponent(param.name, param.driveInfo, param.rangeMin, param.rangeMax, param.textLabels));
            addAndMakeVisible(knob);
        }
    }

    resized(); // also recomputes scopeTopInset from the new knob count
    repaint();
}

void DSPComponent::timerCallback() {
    auto& synth = processor.getSynth();

    if (processor.getSynth().getMapping(processor.getMappingStrategy()) != monitoredMapping) {
        rebuildMonitorKnobs();
    }
    for (int i = 0; i < monitorKnobs.size(); ++i) {
        monitorKnobs.getUnchecked(i)->setValue(monitoredMapping->getMonitorParam(i).value.load(std::memory_order_relaxed));
    }

    if (spectrumAnalyser.getLatestMagnitudesDb(spectrumDb)) {
        auto scopeBounds = getLocalBounds().withTrimmedTop(scopeTopInset).reduced(10).toFloat();
        spectrumPath.clear();

        constexpr float minDB = -100.0f;
        constexpr float maxDB = 0.0f;

        const float sampleRate = static_cast<float>(processor.getSynth().getSampleRate());
        const float maxHz = sampleRate > 0.0f ? sampleRate / 2.0f : 20000.0f;
        const float binHz = sampleRate / static_cast<float>(SpectrumAnalyserThread::fftSize);

        for (size_t i = 0; i < spectrumDb.size(); ++i) {
            float hz = static_cast<float>(i) * binHz;
            float x = freqToLogX(hz, kSpectrumMinHz, maxHz, scopeBounds.getX(), scopeBounds.getRight());
            float y = juce::jmap(spectrumDb[i], minDB, maxDB, scopeBounds.getBottom(), scopeBounds.getY());

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

    auto scopeBounds = getLocalBounds().withTrimmedTop(scopeTopInset).reduced(10);
    g.setColour(Palette::panel);
    g.fillRect(scopeBounds);

    float maxHz = processor.getSynth().getSampleRate() / 2.0f;
    if (maxHz > 0.0f) {
        // Standard analyzer marks, evenly spaced in log space (each decade
        // gets denser marks near its low end, like a real spectrum analyzer).
        static constexpr float niceMarks[] = {20.f, 50.f, 100.f, 200.f, 500.f,
                                               1000.f, 2000.f, 5000.f, 10000.f, 20000.f};

        g.setFont(9.5f);
        for (float hz : niceMarks) {
            if (hz < kSpectrumMinHz || hz > maxHz)
                continue;

            int x = static_cast<int>(freqToLogX(hz, kSpectrumMinHz, maxHz,
                                                 static_cast<float>(scopeBounds.getX()),
                                                 static_cast<float>(scopeBounds.getRight())));

            g.setColour(Palette::border.withAlpha(0.35f));
            g.drawVerticalLine(x, static_cast<float>(scopeBounds.getY()), static_cast<float>(scopeBounds.getBottom()));

            juce::String label = hz >= 1000.0f
                ? juce::String(hz / 1000.0f, (hz < 10000.0f ? 1 : 0)) + "k"
                : juce::String(static_cast<int>(hz));

            g.setColour(Palette::textMid.withAlpha(0.8f));
            g.drawText(label, x - 20, scopeBounds.getBottom() - 14, 40, 12, juce::Justification::centred, false);
        }
    }

    g.setColour(Palette::accentDim);
    g.strokePath(spectrumPath, juce::PathStrokeType(1.5f));

    if (maxHz > 0.0f) {
        float lpfHz = processor.getSynth().getCurrentLpfCutoff();
        float lpfX = freqToLogX(lpfHz, kSpectrumMinHz, maxHz,
                                 static_cast<float>(scopeBounds.getX()), static_cast<float>(scopeBounds.getRight()));

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

    if (monitorKnobs.isEmpty()) {
        scopeTopInset = 40;
    } else {
        constexpr int kKnobCellWidth  = 108; // 100 knob + 4px margin either side
        constexpr int kKnobCellHeight = 152; // 144 knob + 4px margin either side

        bounds.removeFromTop(8);
        const int itemsPerRow = juce::jmax(1, bounds.getWidth() / kKnobCellWidth);
        const int numRows = (monitorKnobs.size() + itemsPerRow - 1) / itemsPerRow;
        const int monitorAreaHeight = numRows * kKnobCellHeight;
        auto monitorArea = bounds.removeFromTop(monitorAreaHeight);

        juce::FlexBox monitorFlexBox;
        monitorFlexBox.flexWrap = juce::FlexBox::Wrap::wrap;
        monitorFlexBox.justifyContent = juce::FlexBox::JustifyContent::flexStart;
        monitorFlexBox.alignContent = juce::FlexBox::AlignContent::flexStart;
        for (auto* knob : monitorKnobs)
            monitorFlexBox.items.add(juce::FlexItem(*knob).withWidth(100.0f).withHeight(144.0f).withMargin(4.0f));
        monitorFlexBox.performLayout(monitorArea.toFloat());

        scopeTopInset = 40 + 8 + monitorAreaHeight;
    }
}

DSPWindow::DSPWindow(REMORAProcessor& p)
    : juce::DocumentWindow("DSP Panel", Palette::bg, juce::DocumentWindow::allButtons),
      dspComponent(p)
{
    setContentNonOwned(&dspComponent, true);
    setResizable(true, true);
    setResizeLimits(400, 300, 2000, 2000);
    setSize(680, 480);
    setUsingNativeTitleBar(true);
}

void DSPWindow::closeButtonPressed() {
    setVisible(false);
}