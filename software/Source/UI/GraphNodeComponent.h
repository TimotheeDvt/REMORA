#pragma once

#include "../DSP/Graph/NodeTypeRegistry.h"
#include "GraphPinComponent.h"
#include <JuceHeader.h>
#include <vector>

class GraphEditorComponent;

class GraphNodeComponent : public juce::Component, public juce::TooltipClient {
public:
    static constexpr int kWidth = 140;
    static constexpr int kHeaderHeight = 22;
    static constexpr int kParamRowHeight = 18;
    static constexpr int kSliderRowHeight = 18;
    static constexpr int kRowHeight = 20;
    static constexpr int kPinSize = 12;

    GraphNodeComponent(GraphEditorComponent& editor, Graph::NodeId nodeId, const Graph::NodeTypeInfo& typeInfo,
                       std::vector<float> params);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

    Graph::NodeId getNodeId() const noexcept { return nodeId; }
    static int preferredHeight(const Graph::NodeTypeInfo& typeInfo);

    juce::Point<int> getInputPinCentre(int port) const;
    juce::Point<int> getOutputPinCentre(int port) const;

private:
    GraphEditorComponent& editor;
    Graph::NodeId nodeId;
    const Graph::NodeTypeInfo& typeInfo;
    std::vector<float> params;

    juce::OwnedArray<GraphPinComponent> inputPins;
    juce::OwnedArray<GraphPinComponent> outputPins;
    juce::OwnedArray<juce::Label> paramNameLabels;
    juce::OwnedArray<juce::TextEditor> paramEditors;
    std::unique_ptr<juce::Slider> valueSlider;
    juce::Point<int> dragStartPos;

    juce::Rectangle<int> infoButtonBounds;

    int portsTop() const noexcept;
    static int paramsHeight(const Graph::NodeTypeInfo& typeInfo);
    static bool hasValueSlider(const Graph::NodeTypeInfo& typeInfo);
    juce::String paramLabelFor(size_t index) const;
    void showInfoPopup();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphNodeComponent)
};
