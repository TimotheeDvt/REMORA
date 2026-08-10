#pragma once

#include "GraphEditorComponent.h"
#include "MappingSelectorBar.h"
#include "SpectrumAnalyserThread.h"
#include <JuceHeader.h>

class REMORAProcessor;
class IMappingStrategy;

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

    void setActive(bool shouldBeActive);

private:
    REMORAProcessor& processor;

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

class DSPTabbedComponent : public juce::TabbedComponent {
public:
    using juce::TabbedComponent::TabbedComponent;

    std::function<void(int newCurrentTabIndex)> onTabChanged;

    void resized() override {
        juce::TabbedComponent::resized();

        auto& bar = getTabbedButtonBar();
        const int numTabs = bar.getNumTabs();
        int totalWidth = 0;
        for (int i = 0; i < numTabs; ++i)
            if (auto* button = bar.getTabButton(i))
                totalWidth += button->getWidth();

        const int offset = (bar.getWidth() - totalWidth) / 2;
        if (offset <= 0)
            return; // tabs already fill (or overflow) the bar - leave JUCE's own layout alone

        for (int i = 0; i < numTabs; ++i)
            if (auto* button = bar.getTabButton(i))
                button->setTopLeftPosition(button->getX() + offset, button->getY());
    }

private:
    void currentTabChanged(int newCurrentTabIndex, const juce::String&) override {
        if (onTabChanged)
            onTabChanged(newCurrentTabIndex);
    }
};

class DSPRootComponent : public juce::Component {
public:
    static constexpr int kSelectorBarHeight = 96;

    DSPRootComponent(MappingSelectorBar& barIn, DSPTabbedComponent& tabsIn) : bar(barIn), tabs(tabsIn) {
        addAndMakeVisible(bar);
        addAndMakeVisible(tabs);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bar.setBounds(bounds.removeFromTop(kSelectorBarHeight));
        tabs.setBounds(bounds);
    }

private:
    MappingSelectorBar& bar;
    DSPTabbedComponent& tabs;
};

class DSPWindow : public juce::DocumentWindow {
public:
    explicit DSPWindow(REMORAProcessor& p);
    void closeButtonPressed() override;

private:
    MappingSelectorBar selectorBar;
    DSPComponent dspComponent;
    GraphEditorComponent graphEditorComponent;
    DSPTabbedComponent tabs { juce::TabbedButtonBar::Orientation::TabsAtTop };
    DSPRootComponent root { selectorBar, tabs };
    juce::TooltipWindow tooltipWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSPWindow)
};