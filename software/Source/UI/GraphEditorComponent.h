#pragma once

#include "../DSP/Graph/NodeGraph.h"
#include "GraphCanvasComponent.h"
#include "GraphNodeComponent.h"
#include <JuceHeader.h>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

class REMORAProcessor;
class GraphPinComponent;
class GraphSearchPopup;

class GraphEditorComponent : public juce::Component {
public:
    explicit GraphEditorComponent(REMORAProcessor& p);
    ~GraphEditorComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Called by DSPWindow whenever the shared mapping selector changes.
    void onMappingChanged();

    bool isGraphEditable() const noexcept { return isEditable; }

    // Live value a node is currently outputting on the given port, for hover tooltips. 0 if no graph is live.
    float liveOutputValue(Graph::NodeId id, int port) const;

    std::function<void(bool)> onDirtyStateChanged;
    bool isCurrentGraphDirty() const noexcept { return isDirty; }
    void resetCurrentGraphToOriginal();

    void markCurrentGraphAsSaved();

    void rerunAutoLayout();

    // ranksep: gap between columns (rank axis, flow direction). nodesep: gap between lanes (rows).
    void setLayoutSpacing(float newRankSep, float newNodeSep);
    float getRankSep() const noexcept { return rankSep; }
    float getNodeSep() const noexcept { return nodeSep; }

    // Called by GraphNodeComponent's inline param TextEditors as they're edited.
    void updateNodeParam(Graph::NodeId id, int index, float value);

    // Called by GraphNodeComponent's inline label editor (double-click on the header caption).
    void updateNodeLabel(Graph::NodeId id, juce::String label);

    // Called by GraphCanvasComponent - draws in canvas-local (world) space.
    void paintConnections(juce::Graphics& g);

    // Mouse events arrive in screen space (pre-zoom/pan); conversion to world space happens where each is used.
    void nodeMoved(Graph::NodeId id, float x, float y);
    void nodeResized(Graph::NodeId id, float w, float h);
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

    bool isNodeSelected(Graph::NodeId id) const { return selectedNodeIds.count(id) > 0; }
    void selectNode(Graph::NodeId id, bool toggle);
    void beginGroupDrag();
    void dragSelectedNodesBy(juce::Point<int> delta);
    void deleteSelectedNodes();

    void goToNode(Graph::NodeId id);
    void addNodeAtViewCentre(const juce::String& typeId);

private:
    REMORAProcessor& processor;
    Graph::NodeGraph* currentGraph = nullptr;
    bool isEditable = false;
    std::set<Graph::NodeGraph*> autoLaidOutGraphs;
    float rankSep = 60.0f;
    float nodeSep = 24.0f;

    std::unordered_map<Graph::NodeGraph*, juce::String> originalSnapshots;
    std::unordered_map<Graph::NodeGraph*, bool> dirtyByGraph;
    bool isDirty = false;

    juce::Label statusLabel;
    GraphCanvasComponent canvas { *this };
    std::unique_ptr<GraphSearchPopup> searchPopup;

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

    std::set<Graph::NodeId> selectedNodeIds;
    std::unordered_map<Graph::NodeId, juce::Point<int>> groupDragStartPositions;
    bool isMarqueeSelecting = false;
    juce::Point<float> marqueeStartCanvas;
    juce::Point<float> marqueeCurrentCanvas;

    void showAddNodeMenu(juce::Point<int> position);
    void addNodeAt(const juce::String& typeId, juce::Point<int> position);
    void deleteNode(Graph::NodeId id);
    void beginMarqueeSelection(const juce::MouseEvent& e);
    void updateMarqueeSelection(const juce::MouseEvent& e);
    void endMarqueeSelection();
    void syncFromModel();
    void autoLayout();
    void updateTransform();
    void markDirty();

    void fixupOrderingAround(Graph::NodeId movedId);
    void pushNodeAndDownstream(Graph::NodeId id, float minX,
                               const std::unordered_map<Graph::NodeId, std::vector<Graph::NodeId>>& children);
    const Graph::NodeInstance* findNode(Graph::NodeId id) const;
    GraphPinComponent* findPinAt(juce::Point<int> posInEditor);
    bool findConnectionAt(juce::Point<float> posInCanvas, Graph::NodeId& outDstNode, int& outDstPort) const;
    void showWireContextMenu(Graph::NodeId dstNode, int dstPort);
    static juce::Path buildConnectionPath(juce::Point<float> from, juce::Point<float> to);
    static void drawConnection(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphEditorComponent)
};
