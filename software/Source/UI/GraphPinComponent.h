#pragma once

#include "../DSP/Graph/GraphTypes.h"
#include <JuceHeader.h>

class GraphEditorComponent;

class GraphPinComponent : public juce::Component, public juce::TooltipClient {
public:
    GraphPinComponent(GraphEditorComponent& editor, Graph::NodeId nodeId, int port, bool isOutput);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

    Graph::NodeId getNodeId() const noexcept { return nodeId; }
    int getPort() const noexcept { return port; }
    bool isOutputPin() const noexcept { return output; }

private:
    GraphEditorComponent& editor;
    Graph::NodeId nodeId;
    int port;
    bool output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphPinComponent)
};
