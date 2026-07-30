#pragma once

#include <JuceHeader.h>

class REMORAProcessor;

class GraphEditorComponent : public juce::Component {
public:
    explicit GraphEditorComponent(REMORAProcessor& p);

    void paint(juce::Graphics& g) override;

private:
    REMORAProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphEditorComponent)
};
