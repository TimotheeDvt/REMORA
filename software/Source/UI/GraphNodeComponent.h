#pragma once

#include "../DSP/Graph/NodeTypeRegistry.h"
#include "GraphPinComponent.h"
#include <JuceHeader.h>
#include <vector>

class GraphEditorComponent;

class GraphNodeComponent : public juce::Component, public juce::TooltipClient, private juce::Timer {
public:
    static constexpr int kWidth = 140;
    static constexpr int kHeaderHeight = 22;
    static constexpr int kParamRowHeight = 18;
    static constexpr int kSliderRowHeight = 18;
    static constexpr int kRowHeight = 20;
    static constexpr int kPinSize = 12;
    static constexpr int kResizeGripSize = 14;
    static constexpr int kMinDisplayWidth = 70;
    static constexpr int kMinDisplayHeight = 50;

    GraphNodeComponent(GraphEditorComponent& editor, Graph::NodeId nodeId, const Graph::NodeTypeInfo& typeInfo,
                       std::vector<float> params, float initialW = 0.0f, float initialH = 0.0f,
                       juce::String label = {});

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

    void startHighlight();

private:
    GraphEditorComponent& editor;
    Graph::NodeId nodeId;
    const Graph::NodeTypeInfo& typeInfo;
    std::vector<float> params;
    juce::String nodeLabel;

    juce::OwnedArray<GraphPinComponent> inputPins;
    juce::OwnedArray<GraphPinComponent> outputPins;
    juce::OwnedArray<juce::Label> paramNameLabels;
    juce::OwnedArray<juce::TextEditor> paramEditors;
    std::unique_ptr<juce::Slider> valueSlider;

    juce::Rectangle<int> infoButtonBounds;

    double highlightStartMs = 0.0;
    bool highlightActive = false;
    void timerCallback() override;

    bool isResizing = false;
    juce::Point<int> resizeStartSize;
    std::vector<float> scopeHistory;

    int portsTop() const noexcept;
    static int paramsHeight(const Graph::NodeTypeInfo& typeInfo);
    static bool hasValueSlider(const Graph::NodeTypeInfo& typeInfo);
    juce::String paramLabelFor(size_t index) const;
    juce::String displayCaption() const { return nodeLabel.isNotEmpty() ? nodeLabel : typeInfo.displayName; }
    void showInfoPopup();
    void paintDisplay(juce::Graphics& g, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphNodeComponent)
};
