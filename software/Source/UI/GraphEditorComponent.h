#pragma once

#include "../DSP/Graph/NodeGraph.h"
#include "GraphCanvasComponent.h"
#include "GraphNodeComponent.h"
#include <JuceHeader.h>
#include <set>
#include <unordered_map>

class REMORAProcessor;
class GraphPinComponent;

class GraphEditorComponent : public juce::Component {
public:
    explicit GraphEditorComponent(REMORAProcessor& p);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Called by DSPWindow whenever the shared mapping selector changes.
    void onMappingChanged();

    bool isGraphEditable() const noexcept { return isEditable; }

    // Called by GraphNodeComponent's inline param TextEditors as they're edited.
    void updateNodeParam(Graph::NodeId id, int index, float value);

    // Called by GraphCanvasComponent - draws in canvas-local (world) space.
    void paintConnections(juce::Graphics& g);

    // Mouse events arrive in screen space (pre-zoom/pan); conversion to world space happens where each is used.
    void nodeMoved(Graph::NodeId id, float x, float y);
    void showNodeContextMenu(Graph::NodeId id);
    void showPinContextMenu(GraphPinComponent& pin);
    void handlePinMouseDown(GraphPinComponent& pin, const juce::MouseEvent& e);
    void handlePinMouseDrag(const juce::MouseEvent& e);
    void handlePinMouseUp(const juce::MouseEvent& e);
    void handleCanvasMouseDown(const juce::MouseEvent& e);
    void handleCanvasMouseDrag(const juce::MouseEvent& e);
    void handleCanvasMouseUp();

    void handleBackgroundMouseDown(const juce::MouseEvent& e);
    void handleBackgroundMouseDrag(const juce::MouseEvent& e);

private:
    REMORAProcessor& processor;
    Graph::NodeGraph* currentGraph = nullptr;
    bool isEditable = false;
    std::set<Graph::NodeGraph*> autoLaidOutGraphs;

    juce::Label statusLabel;
    GraphCanvasComponent canvas { *this };

    float zoom = 1.0f;
    juce::Point<float> panOffset;
    bool isPanning = false;
    juce::Point<float> panDragStartOffset;
    juce::Point<float> panDragStartMouse;

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
    void autoLayout();
    void updateTransform();
    GraphPinComponent* findPinAt(juce::Point<int> posInEditor);
    bool findConnectionAt(juce::Point<float> posInCanvas, Graph::NodeId& outDstNode, int& outDstPort) const;
    void showWireContextMenu(Graph::NodeId dstNode, int dstPort);
    static juce::Path buildConnectionPath(juce::Point<float> from, juce::Point<float> to);
    static void drawConnection(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphEditorComponent)
};
