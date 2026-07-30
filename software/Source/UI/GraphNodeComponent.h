#pragma once

#include "../DSP/Graph/NodeTypeRegistry.h"
#include "GraphPinComponent.h"
#include <JuceHeader.h>

class GraphEditorComponent;

class GraphNodeComponent : public juce::Component {
public:
    static constexpr int kWidth = 140;
    static constexpr int kHeaderHeight = 22;
    static constexpr int kRowHeight = 20;
    static constexpr int kPinSize = 12;

    GraphNodeComponent(GraphEditorComponent& editor, Graph::NodeId nodeId, const Graph::NodeTypeInfo& typeInfo);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    Graph::NodeId getNodeId() const noexcept { return nodeId; }
    static int preferredHeight(const Graph::NodeTypeInfo& typeInfo);

    juce::Point<int> getInputPinCentre(int port) const;
    juce::Point<int> getOutputPinCentre(int port) const;

private:
    GraphEditorComponent& editor;
    Graph::NodeId nodeId;
    const Graph::NodeTypeInfo& typeInfo;

    juce::OwnedArray<GraphPinComponent> inputPins;
    juce::OwnedArray<GraphPinComponent> outputPins;
    juce::Point<int> dragStartPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphNodeComponent)
};
