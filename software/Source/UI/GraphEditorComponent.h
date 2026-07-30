#pragma once

#include "../DSP/Graph/NodeGraph.h"
#include "GraphNodeComponent.h"
#include <JuceHeader.h>
#include <unordered_map>

class REMORAProcessor;
class GraphPinComponent;

class GraphEditorComponent : public juce::Component {
public:
    explicit GraphEditorComponent(REMORAProcessor& p);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    void nodeMoved(Graph::NodeId id, float x, float y);
    void showNodeContextMenu(Graph::NodeId id);
    void showPinContextMenu(GraphPinComponent& pin);
    void handlePinMouseDown(GraphPinComponent& pin, const juce::MouseEvent& e);
    void handlePinMouseDrag(const juce::MouseEvent& e);
    void handlePinMouseUp(const juce::MouseEvent& e);

private:
    REMORAProcessor& processor;
    std::unique_ptr<Graph::NodeGraph> graph;

    juce::OwnedArray<GraphNodeComponent> nodeComponents;
    std::unordered_map<Graph::NodeId, GraphNodeComponent*> nodeComponentById;

    bool isDraggingConnector = false;
    Graph::NodeId dragSourceNode = Graph::kInvalidNodeId;
    int dragSourcePort = 0;
    bool dragSourceIsOutput = false;
    juce::Point<float> dragCurrentPos;

    void showAddNodeMenu(juce::Point<int> position);
    void addNodeAt(const juce::String& typeId, juce::Point<int> position);
    void deleteNode(Graph::NodeId id);
    void syncFromModel();
    GraphPinComponent* findPinAt(juce::Point<int> posInEditor) const;
    static void drawConnection(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphEditorComponent)
};
