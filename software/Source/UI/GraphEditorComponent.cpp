#include "GraphEditorComponent.h"
#include "../DSP/Graph/GraphMappingStrategy.h"
#include "../DSP/Graph/NodeTypeRegistry.h"
#include "../PluginProcessor.h"
#include "DebugLog.h"
#include "GraphPinComponent.h"
#include "GraphSearchPopup.h"
#include "Palette.h"
#include "StyleHelpers.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace {
constexpr int kCanvasWidth = 100000;
constexpr int kCanvasHeight = 100000;
constexpr float kCanvasOriginX = kCanvasWidth * 0.5f;
constexpr float kCanvasOriginY = kCanvasHeight * 0.5f;
constexpr float kMinZoom = 0.1f, kMaxZoom = 3.0f;
} // namespace

GraphEditorComponent::GraphEditorComponent(REMORAProcessor& p) : processor(p) {
    setWantsKeyboardFocus(true);
    styleLabel(statusLabel, {}, 13.0f, Palette::textMid, juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    canvas.setSize(kCanvasWidth, kCanvasHeight);
    canvas.setBufferedToImage(false);
    addAndMakeVisible(canvas);

    searchPopup = std::make_unique<GraphSearchPopup>(*this);
    addChildComponent(*searchPopup);

    panOffset = { -kCanvasOriginX, -kCanvasOriginY };
    updateTransform();
}

GraphEditorComponent::~GraphEditorComponent() = default;

void GraphEditorComponent::onMappingChanged() {
    auto* mapping = processor.getSynth().getMapping(processor.getMappingStrategy());
    auto* graphMapping = dynamic_cast<Graph::GraphMappingStrategy*>(mapping);
    currentGraph = graphMapping != nullptr ? &graphMapping->getGraph() : nullptr;

    const juce::String name = mapping != nullptr ? mapping->getName() : juce::String();
    isEditable = graphMapping != nullptr;

    statusLabel.setText(isEditable ? "Editing: " + name : "Viewing: " + name, juce::dontSendNotification);

    if (currentGraph != nullptr) {
        const Graph::NodeCounts counts = currentGraph->countNodesByCategory();
        debug.print.cyan("Preset '" + name + "' node counts - total:", counts.total, "source:", counts.source,
                          "math:", counts.math, "sink:", counts.sink, "display:", counts.display);
    }

    if (currentGraph != nullptr && autoLaidOutGraphs.insert(currentGraph).second) {
        autoLayout();
        originalSnapshots[currentGraph] = currentGraph->toXml()->toString();
    }

    isDirty = currentGraph != nullptr && dirtyByGraph.count(currentGraph) > 0 && dirtyByGraph[currentGraph];
    if (onDirtyStateChanged)
        onDirtyStateChanged(isDirty);

    selectedNodeIds.clear();
    nodeComponents.clear();
    nodeComponentById.clear();
    syncFromModel();

    searchPopup->setContext(currentGraph, isEditable);
}

float GraphEditorComponent::liveOutputValue(Graph::NodeId id, int port) const {
    return currentGraph != nullptr ? currentGraph->outputOf(id, port) : 0.0f;
}

void GraphEditorComponent::markDirty() {
    if (currentGraph == nullptr)
        return;
    dirtyByGraph[currentGraph] = true;
    if (!isDirty) {
        isDirty = true;
        if (onDirtyStateChanged)
            onDirtyStateChanged(true);
    }
}

void GraphEditorComponent::resetCurrentGraphToOriginal() {
    if (currentGraph == nullptr)
        return;
    auto it = originalSnapshots.find(currentGraph);
    if (it == originalSnapshots.end())
        return;

    auto xml = juce::parseXML(it->second);
    if (xml == nullptr || !currentGraph->resetFromXml(*xml))
        return;

    dirtyByGraph[currentGraph] = false;
    isDirty = false;
    if (onDirtyStateChanged)
        onDirtyStateChanged(false);

    selectedNodeIds.clear();
    nodeComponents.clear();
    nodeComponentById.clear();
    syncFromModel();
    canvas.repaint();
}

void GraphEditorComponent::markCurrentGraphAsSaved() {
    if (currentGraph == nullptr)
        return;
    originalSnapshots[currentGraph] = currentGraph->toXml()->toString();
    dirtyByGraph[currentGraph] = false;
    isDirty = false;
    if (onDirtyStateChanged)
        onDirtyStateChanged(false);
}

void GraphEditorComponent::setLayoutSpacing(float newRankSep, float newNodeSep) {
    rankSep = newRankSep;
    nodeSep = newNodeSep;
    rerunAutoLayout();
}

void GraphEditorComponent::rerunAutoLayout() {
    if (currentGraph == nullptr || !isEditable)
        return;

    autoLayout();
    markDirty();

    nodeComponents.clear();
    nodeComponentById.clear();
    syncFromModel();
    canvas.repaint();
}

// Reimplements d3-dag's grid layout (https://github.com/erikbrinkman/d3-dag)
// but with a few tweaks to make it more suitable for our use case and more compatible horizontally.
void GraphEditorComponent::autoLayout() {
    if (currentGraph == nullptr)
        return;

    const auto& nodes = currentGraph->nodes();
    if (nodes.empty())
        return;

    // Raw (possibly duplicated, one per connected input slot) parent -> child edges, used to compute a
    // topological order via Kahn's algorithm.
    std::unordered_map<Graph::NodeId, std::vector<Graph::NodeId>> rawChildrenOf;
    std::unordered_map<Graph::NodeId, int> pendingParents;
    for (const auto& n : nodes)
        pendingParents[n.id] = 0;
    for (const auto& n : nodes)
        for (const auto& slot : n.inputs)
            if (slot.sourceNode != Graph::kInvalidNodeId) {
                rawChildrenOf[slot.sourceNode].push_back(n.id);
                ++pendingParents[n.id];
            }

    std::vector<Graph::NodeId> ordered;
    ordered.reserve(nodes.size());
    {
        std::vector<Graph::NodeId> ready;
        for (const auto& n : nodes)
            if (pendingParents[n.id] == 0)
                ready.push_back(n.id);

        for (size_t head = 0; head < ready.size(); ++head) {
            const Graph::NodeId id = ready[head];
            ordered.push_back(id);
            auto it = rawChildrenOf.find(id);
            if (it == rawChildrenOf.end())
                continue;
            for (Graph::NodeId child : it->second)
                if (--pendingParents[child] == 0)
                    ready.push_back(child);
        }

        // A cycle shouldn't be possible (NodeGraph::connect() rejects them), but rather than drop
        // nodes if one slips through, just tack on whatever's left in its original order.
        if (ordered.size() < nodes.size()) {
            std::set<Graph::NodeId> seen(ordered.begin(), ordered.end());
            for (const auto& n : nodes)
                if (seen.count(n.id) == 0)
                    ordered.push_back(n.id);
        }
    }

    std::unordered_map<Graph::NodeId, int> rankOf;
    for (const auto& n : nodes)
        rankOf[n.id] = 0;
    for (Graph::NodeId id : ordered) {
        auto it = rawChildrenOf.find(id);
        if (it == rawChildrenOf.end())
            continue;
        for (Graph::NodeId child : it->second)
            rankOf[child] = std::max(rankOf[child], rankOf[id] + 1);
    }

    // Deduplicated children per node (d3-dag's gridChildren is a Set), used for lane assignment.
    std::unordered_map<Graph::NodeId, std::vector<Graph::NodeId>> childrenOf = rawChildrenOf;
    for (auto& [id, kids] : childrenOf) {
        std::sort(kids.begin(), kids.end());
        kids.erase(std::unique(kids.begin(), kids.end()), kids.end());
    }

    // Greedy lane assignment, mirroring d3-dag's laneGreedy() defaults (topDown, compressed,
    // one-sided). laneReservedUntil[lane] is the rank up to which that lane is claimed by an
    // in-flight edge; a lane becomes eligible again once we've passed that rank.
    std::vector<int> laneReservedUntil;
    auto pickLane = [&](int checkAfter, int storeAsRank, int targetLane) {
        int best = -1;
        for (int lane = 0; lane < static_cast<int>(laneReservedUntil.size()); ++lane) {
            if (laneReservedUntil[static_cast<size_t>(lane)] > checkAfter)
                continue;
            const int dist = std::abs(targetLane - lane);
            const int bestDist = best == -1 ? 0 : std::abs(targetLane - best);
            if (best == -1 || dist < bestDist || (dist == bestDist && lane < best))
                best = lane;
        }
        const int chosen = best == -1 ? static_cast<int>(laneReservedUntil.size()) : best;
        if (chosen == static_cast<int>(laneReservedUntil.size()))
            laneReservedUntil.push_back(storeAsRank);
        else
            laneReservedUntil[static_cast<size_t>(chosen)] = storeAsRank;
        return chosen;
    };

    std::unordered_map<Graph::NodeId, int> laneOf;
    for (Graph::NodeId id : ordered) {
        if (laneOf.count(id) == 0)
            laneOf[id] = pickLane(rankOf[id] - 1, rankOf[id], 0);

        auto it = childrenOf.find(id);
        if (it == childrenOf.end())
            continue;

        // Farthest-away children get first pick of a lane close to their parent's.
        std::vector<Graph::NodeId> kids = it->second;
        std::sort(kids.begin(), kids.end(), [&](Graph::NodeId a, Graph::NodeId b) { return rankOf[a] > rankOf[b]; });
        for (Graph::NodeId child : kids) {
            if (laneOf.count(child) > 0)
                continue;
            laneOf[child] = pickLane(rankOf[id], rankOf[child], laneOf[id]);
        }
    }

    // Rank -> x (all nodes share a fixed width, so this is just the usual column spacing).
    // Lane -> y (each lane is a row as tall as the tallest node ever placed in it).
    const float columnGap = static_cast<float>(GraphNodeComponent::kWidth) + rankSep;
    const float rowGap = nodeSep;

    const int numLanes = static_cast<int>(laneReservedUntil.size());
    std::vector<float> laneHeight(static_cast<size_t>(numLanes), 0.0f);
    for (const auto& n : nodes) {
        const Graph::NodeTypeInfo* info = Graph::NodeTypeRegistry::instance().find(n.typeId);
        const float h = static_cast<float>(info != nullptr ? GraphNodeComponent::preferredHeight(*info)
                                                            : GraphNodeComponent::kHeaderHeight);
        float& slot = laneHeight[static_cast<size_t>(laneOf[n.id])];
        slot = std::max(slot, h);
    }

    std::vector<float> laneTop(static_cast<size_t>(numLanes), 0.0f);
    float y = 0.0f;
    for (int lane = 0; lane < numLanes; ++lane) {
        laneTop[static_cast<size_t>(lane)] = y;
        y += laneHeight[static_cast<size_t>(lane)] + rowGap;
    }

    for (const auto& n : nodes) {
        const float x = static_cast<float>(rankOf[n.id]) * columnGap;
        currentGraph->setNodePosition(n.id, x, laneTop[static_cast<size_t>(laneOf[n.id])]);
    }
}

const Graph::NodeInstance* GraphEditorComponent::findNode(Graph::NodeId id) const {
    if (currentGraph == nullptr)
        return nullptr;
    for (const auto& n : currentGraph->nodes())
        if (n.id == id)
            return &n;
    return nullptr;
}

void GraphEditorComponent::pushNodeAndDownstream(Graph::NodeId id, float minX,
        const std::unordered_map<Graph::NodeId, std::vector<Graph::NodeId>>& children) {
    const Graph::NodeInstance* node = findNode(id);
    if (node == nullptr || node->x >= minX)
        return;

    currentGraph->setNodePosition(id, minX, node->y);

    constexpr float columnGap = static_cast<float>(GraphNodeComponent::kWidth) + 40.0f;
    auto it = children.find(id);
    if (it != children.end())
        for (Graph::NodeId child : it->second)
            pushNodeAndDownstream(child, minX + columnGap, children);
}

void GraphEditorComponent::fixupOrderingAround(Graph::NodeId movedId) {
    if (currentGraph == nullptr)
        return;

    std::unordered_map<Graph::NodeId, std::vector<Graph::NodeId>> children;
    for (const auto& n : currentGraph->nodes())
        for (const auto& slot : n.inputs)
            if (slot.sourceNode != Graph::kInvalidNodeId)
                children[slot.sourceNode].push_back(n.id);

    constexpr float columnGap = static_cast<float>(GraphNodeComponent::kWidth) + 40.0f;

    // If movedId now sits at or left of one of its own sources, push it (and everything downstream of it) right.
    if (const Graph::NodeInstance* moved = findNode(movedId))
        for (const auto& slot : moved->inputs)
            if (slot.sourceNode != Graph::kInvalidNodeId)
                if (const Graph::NodeInstance* source = findNode(slot.sourceNode))
                    pushNodeAndDownstream(movedId, source->x + columnGap, children);

    // If movedId now sits at or right of one of the nodes it feeds, push that node (and its own downstream) right.
    auto it = children.find(movedId);
    if (it != children.end())
        if (const Graph::NodeInstance* moved = findNode(movedId))
            for (Graph::NodeId child : it->second)
                pushNodeAndDownstream(child, moved->x + columnGap, children);
}

void GraphEditorComponent::syncFromModel() {
    if (currentGraph == nullptr)
        return;

    for (int i = nodeComponents.size(); --i >= 0;) {
        auto* comp = nodeComponents.getUnchecked(i);
        const bool stillExists = std::any_of(currentGraph->nodes().begin(), currentGraph->nodes().end(),
            [comp](const Graph::NodeInstance& n) { return n.id == comp->getNodeId(); });
        if (!stillExists) {
            nodeComponentById.erase(comp->getNodeId());
            nodeComponents.remove(i);
        }
    }

    for (const auto& n : currentGraph->nodes()) {
        if (nodeComponentById.count(n.id) > 0)
            continue;
        const Graph::NodeTypeInfo* info = Graph::NodeTypeRegistry::instance().find(n.typeId);
        if (info == nullptr)
            continue;
        auto* comp = nodeComponents.add(new GraphNodeComponent(*this, n.id, *info, n.params, n.w, n.h, n.label));
        comp->setTopLeftPosition(static_cast<int>(n.x + kCanvasOriginX), static_cast<int>(n.y + kCanvasOriginY));
        canvas.addAndMakeVisible(comp);
        nodeComponentById[n.id] = comp;
    }

    canvas.repaint();
}

void GraphEditorComponent::updateNodeParam(Graph::NodeId id, int index, float value) {
    if (!isEditable || currentGraph == nullptr)
        return;
    currentGraph->setNodeParam(id, index, value);
    markDirty();
}

void GraphEditorComponent::updateNodeLabel(Graph::NodeId id, juce::String label) {
    if (!isEditable || currentGraph == nullptr)
        return;
    currentGraph->setNodeLabel(id, std::move(label));
    markDirty();
}

void GraphEditorComponent::showAddNodeMenu(juce::Point<int> position) {
    if (!isEditable || currentGraph == nullptr)
        return;

    auto typeIdByItemId = std::make_shared<std::vector<juce::String>>();
    typeIdByItemId->push_back({}); // item id 0 is "dismissed", unused

    // category -> subcategory -> leaf items, so e.g. Source splits into "Raw Sensor"/"Derived Motion".
    std::map<Graph::NodeCategory, std::map<juce::String, juce::PopupMenu>> menusByCategory;
    // Display has too few node types to need subcategory grouping - flat "Display" -> items instead.
    juce::PopupMenu displayMenu;

    for (const auto& info : Graph::NodeTypeRegistry::instance().all()) {
        typeIdByItemId->push_back(info.id);
        const int itemId = static_cast<int>(typeIdByItemId->size() - 1);
        if (info.category == Graph::NodeCategory::Display)
            displayMenu.addItem(itemId, info.displayName);
        else
            menusByCategory[info.category][info.subcategory].addItem(itemId, info.displayName);
    }

    juce::PopupMenu root;
    auto addCategory = [&](Graph::NodeCategory category, const juce::String& label) {
        auto it = menusByCategory.find(category);
        if (it == menusByCategory.end())
            return;
        juce::PopupMenu categoryMenu;
        for (auto& [subcategory, leafMenu] : it->second)
            categoryMenu.addSubMenu(subcategory.isEmpty() ? juce::String("Other") : subcategory, leafMenu);
        root.addSubMenu(label, categoryMenu);
    };
    addCategory(Graph::NodeCategory::Source, "Source");
    addCategory(Graph::NodeCategory::Math, "Math");
    addCategory(Graph::NodeCategory::Sink, "Sink");
    root.addSubMenu("Display", displayMenu);

    const auto screenPos = localPointToGlobal(position);
    root.showMenuAsync(juce::PopupMenu::Options{}.withTargetScreenArea({ screenPos.x, screenPos.y, 1, 1 }),
        [this, position, typeIdByItemId](int result) {
            if (result > 0 && result < static_cast<int>(typeIdByItemId->size()))
                addNodeAt((*typeIdByItemId)[static_cast<size_t>(result)], position);
        });
}

void GraphEditorComponent::addNodeAt(const juce::String& typeId, juce::Point<int> position) {
    if (currentGraph == nullptr)
        return;
    const auto canvasPos = canvas.getLocalPoint(this, position);
    const Graph::NodeId id = currentGraph->addNode(typeId);
    currentGraph->setNodePosition(id, static_cast<float>(canvasPos.x) - kCanvasOriginX,
                                   static_cast<float>(canvasPos.y) - kCanvasOriginY);
    markDirty();
    syncFromModel();
}

void GraphEditorComponent::addNodeAtViewCentre(const juce::String& typeId) {
    if (!isEditable)
        return;
    addNodeAt(typeId, getLocalBounds().getCentre());
}

void GraphEditorComponent::deleteNode(Graph::NodeId id) {
    if (currentGraph == nullptr)
        return;
    currentGraph->removeNode(id);
    markDirty();
    syncFromModel();
}

void GraphEditorComponent::deleteSelectedNodes() {
    if (currentGraph == nullptr || selectedNodeIds.empty())
        return;
    for (Graph::NodeId id : selectedNodeIds)
        currentGraph->removeNode(id);
    selectedNodeIds.clear();
    markDirty();
    syncFromModel();
}

void GraphEditorComponent::selectNode(Graph::NodeId id, bool toggle) {
    if (toggle) {
        if (!selectedNodeIds.insert(id).second)
            selectedNodeIds.erase(id);
    } else {
        selectedNodeIds.clear();
        selectedNodeIds.insert(id);
    }
    canvas.repaint();
}

void GraphEditorComponent::beginGroupDrag() {
    groupDragStartPositions.clear();
    for (Graph::NodeId id : selectedNodeIds) {
        auto it = nodeComponentById.find(id);
        if (it != nodeComponentById.end())
            groupDragStartPositions[id] = it->second->getPosition();
    }
}

void GraphEditorComponent::dragSelectedNodesBy(juce::Point<int> delta) {
    for (const auto& [id, startPos] : groupDragStartPositions) {
        auto it = nodeComponentById.find(id);
        if (it == nodeComponentById.end())
            continue;
        const auto newPos = startPos + delta;
        it->second->setTopLeftPosition(newPos);
        nodeMoved(id, static_cast<float>(newPos.x), static_cast<float>(newPos.y));
    }
}

void GraphEditorComponent::beginMarqueeSelection(const juce::MouseEvent& e) {
    isMarqueeSelecting = true;
    marqueeStartCanvas = canvas.getLocalPoint(this, e.getPosition()).toFloat();
    marqueeCurrentCanvas = marqueeStartCanvas;
    canvas.repaint();
}

void GraphEditorComponent::updateMarqueeSelection(const juce::MouseEvent& e) {
    marqueeCurrentCanvas = canvas.getLocalPoint(this, e.getPosition()).toFloat();
    canvas.repaint();
}

void GraphEditorComponent::endMarqueeSelection() {
    isMarqueeSelecting = false;
    const juce::Rectangle<float> rect(marqueeStartCanvas, marqueeCurrentCanvas);
    selectedNodeIds.clear();
    for (auto* comp : nodeComponents)
        if (rect.intersects(comp->getBounds().toFloat()))
            selectedNodeIds.insert(comp->getNodeId());
    canvas.repaint();
}

void GraphEditorComponent::nodeMoved(Graph::NodeId id, float x, float y) {
    if (currentGraph == nullptr)
        return;
    currentGraph->setNodePosition(id, x - kCanvasOriginX, y - kCanvasOriginY);
    fixupOrderingAround(id);
    markDirty();
    canvas.repaint();
}

void GraphEditorComponent::nodeResized(Graph::NodeId id, float w, float h) {
    if (currentGraph == nullptr)
        return;
    currentGraph->setNodeSize(id, w, h);
    markDirty();
}

void GraphEditorComponent::showNodeContextMenu(Graph::NodeId id) {
    if (!isEditable)
        return;

    const bool multi = selectedNodeIds.size() > 1 && isNodeSelected(id);
    juce::PopupMenu menu;
    menu.addItem(1, multi ? "Delete " + juce::String(selectedNodeIds.size()) + " Nodes" : "Delete Node");
    menu.showMenuAsync(juce::PopupMenu::Options{}, [this, id, multi](int result) {
        if (result != 1)
            return;
        if (multi)
            deleteSelectedNodes();
        else
            deleteNode(id);
    });
}

void GraphEditorComponent::showPinContextMenu(GraphPinComponent& pin) {
    if (!isEditable || currentGraph == nullptr || pin.isOutputPin())
        return;

    bool connected = false;
    for (const auto& n : currentGraph->nodes()) {
        if (n.id == pin.getNodeId() && pin.getPort() < static_cast<int>(n.inputs.size())) {
            connected = n.inputs[static_cast<size_t>(pin.getPort())].sourceNode != Graph::kInvalidNodeId;
            break;
        }
    }
    if (!connected)
        return;

    const Graph::NodeId nodeId = pin.getNodeId();
    const int port = pin.getPort();
    juce::PopupMenu menu;
    menu.addItem(1, "Disconnect");
    menu.showMenuAsync(juce::PopupMenu::Options{}, [this, nodeId, port](int result) {
        if (result == 1 && currentGraph != nullptr) {
            currentGraph->disconnectInput(nodeId, port);
            markDirty();
            canvas.repaint();
        }
    });
}

void GraphEditorComponent::handlePinMouseDown(GraphPinComponent& pin, const juce::MouseEvent& e) {
    if (!isEditable)
        return;
    isDraggingConnector = true;
    dragSourceNode = pin.getNodeId();
    dragSourcePort = pin.getPort();
    dragSourceIsOutput = pin.isOutputPin();
    dragCurrentPos = e.position;
    canvas.repaint();
}

void GraphEditorComponent::handlePinMouseDrag(const juce::MouseEvent& e) {
    if (!isDraggingConnector)
        return;
    dragCurrentPos = e.position;
    canvas.repaint();
}

void GraphEditorComponent::handlePinMouseUp(const juce::MouseEvent& e) {
    if (!isDraggingConnector)
        return;
    isDraggingConnector = false;

    if (currentGraph != nullptr) {
        if (auto* targetPin = findPinAt(e.getPosition())) {
            if (targetPin->getNodeId() != dragSourceNode && targetPin->isOutputPin() != dragSourceIsOutput) {
                const Graph::NodeId dstNode = dragSourceIsOutput ? targetPin->getNodeId() : dragSourceNode;
                const bool connected = dragSourceIsOutput
                    ? currentGraph->connect(dragSourceNode, dragSourcePort, targetPin->getNodeId(), targetPin->getPort())
                    : currentGraph->connect(targetPin->getNodeId(), targetPin->getPort(), dragSourceNode, dragSourcePort);
                if (connected) {
                    fixupOrderingAround(dstNode);
                    markDirty();
                }
            }
        }
    }
    canvas.repaint();
}

void GraphEditorComponent::handleCanvasMouseDown(const juce::MouseEvent& e) {
    isPanning = true;
    panDragStartOffset = panOffset;
    panDragStartMouse = e.position;
}

void GraphEditorComponent::handleCanvasMouseDrag(const juce::MouseEvent& e) {
    if (!isPanning)
        return;
    panOffset = panDragStartOffset + (e.position - panDragStartMouse);
    updateTransform();
}

void GraphEditorComponent::handleCanvasMouseUp() {
    if (isMarqueeSelecting) {
        endMarqueeSelection();
        return;
    }
    isPanning = false;
    repaint();
    canvas.repaint();
}

void GraphEditorComponent::updateTransform() {
    canvas.setTransform(juce::AffineTransform::scale(zoom).translated(panOffset.x, panOffset.y));
}

GraphPinComponent* GraphEditorComponent::findPinAt(juce::Point<int> posInEditor) {
    const auto posInCanvas = canvas.getLocalPoint(this, posInEditor);
    return dynamic_cast<GraphPinComponent*>(canvas.getComponentAt(posInCanvas));
}

bool GraphEditorComponent::findConnectionAt(juce::Point<float> posInCanvas, Graph::NodeId& outDstNode, int& outDstPort) const {
    if (currentGraph == nullptr)
        return false;

    constexpr float kHitTolerance = 8.0f;
    for (const auto& n : currentGraph->nodes()) {
        auto dstIt = nodeComponentById.find(n.id);
        if (dstIt == nodeComponentById.end())
            continue;
        for (size_t p = 0; p < n.inputs.size(); ++p) {
            const auto& slot = n.inputs[p];
            if (slot.sourceNode == Graph::kInvalidNodeId)
                continue;
            auto srcIt = nodeComponentById.find(slot.sourceNode);
            if (srcIt == nodeComponentById.end())
                continue;

            const auto path = buildConnectionPath(srcIt->second->getOutputPinCentre(slot.sourceOutputPort).toFloat(),
                                                  dstIt->second->getInputPinCentre(static_cast<int>(p)).toFloat());
            juce::Path stroked;
            juce::PathStrokeType(kHitTolerance).createStrokedPath(stroked, path);
            if (stroked.contains(posInCanvas)) {
                outDstNode = n.id;
                outDstPort = static_cast<int>(p);
                return true;
            }
        }
    }
    return false;
}

void GraphEditorComponent::showWireContextMenu(Graph::NodeId dstNode, int dstPort) {
    if (!isEditable || currentGraph == nullptr)
        return;

    const Graph::NodeInstance* dstInst = findNode(dstNode);
    const Graph::NodeId srcNode = (dstInst != nullptr && dstPort < static_cast<int>(dstInst->inputs.size()))
        ? dstInst->inputs[static_cast<size_t>(dstPort)].sourceNode : Graph::kInvalidNodeId;

    juce::PopupMenu menu;
    menu.addItem(1, "Remove Connection");
    menu.addItem(2, "Go to Source", srcNode != Graph::kInvalidNodeId);
    menu.addItem(3, "Go to Destination");
    menu.showMenuAsync(juce::PopupMenu::Options{}, [this, dstNode, dstPort, srcNode](int result) {
        if (result == 1 && currentGraph != nullptr) {
            currentGraph->disconnectInput(dstNode, dstPort);
            markDirty();
            canvas.repaint();
        } else if (result == 2) {
            goToNode(srcNode);
        } else if (result == 3) {
            goToNode(dstNode);
        }
    });
}

void GraphEditorComponent::goToNode(Graph::NodeId id) {
    auto it = nodeComponentById.find(id);
    if (it == nodeComponentById.end())
        return;

    const auto nodeCentre = it->second->getBounds().getCentre().toFloat();
    const auto editorCentre = getLocalBounds().getCentre().toFloat();
    panOffset = editorCentre - nodeCentre * zoom;
    updateTransform();
    canvas.repaint();
    it->second->startHighlight();
}

void GraphEditorComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    handleBackgroundMouseDown(e);
}

void GraphEditorComponent::mouseDrag(const juce::MouseEvent& e) {
    handleBackgroundMouseDrag(e);
}

void GraphEditorComponent::handleBackgroundMouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        Graph::NodeId dstNode = Graph::kInvalidNodeId;
        int dstPort = 0;
        const auto posInCanvas = canvas.getLocalPoint(this, e.getPosition()).toFloat();
        if (isEditable && findConnectionAt(posInCanvas, dstNode, dstPort))
            showWireContextMenu(dstNode, dstPort);
        else
            showAddNodeMenu(e.getPosition());
        return;
    }
    // Shift+drag on empty canvas draws a marquee selection rectangle instead of panning.
    if (e.mods.isShiftDown()) {
        beginMarqueeSelection(e);
        return;
    }
    if (!selectedNodeIds.empty()) {
        selectedNodeIds.clear();
        canvas.repaint();
    }
    // Plain click+drag on empty canvas pans - no modifier key needed.
    handleCanvasMouseDown(e);
}

void GraphEditorComponent::handleBackgroundMouseDrag(const juce::MouseEvent& e) {
    if (isMarqueeSelecting) {
        updateMarqueeSelection(e);
        return;
    }
    handleCanvasMouseDrag(e);
}

void GraphEditorComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const float oldZoom = zoom;
    const float newZoom = juce::jlimit(kMinZoom, kMaxZoom, zoom * std::exp(wheel.deltaY * 2.0f));
    if (std::abs(newZoom - oldZoom) < 1.0e-6f)
        return;

    // Keeps the world point under the cursor fixed on screen, rather than zooming toward the canvas origin.
    const auto mousePos = e.position;
    const auto worldUnderMouse = (mousePos - panOffset) / oldZoom;
    panOffset = mousePos - worldUnderMouse * newZoom;
    zoom = newZoom;
    updateTransform();
    repaint();
    canvas.repaint();
}

bool GraphEditorComponent::keyPressed(const juce::KeyPress& key) {
    if (key.getKeyCode() == 'f') {
        if (searchPopup->isOpen())
            searchPopup->closePopup();
        else
            searchPopup->open();
        return true;
    }
    if (isEditable && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)) {
        deleteSelectedNodes();
        return true;
    }
    return false;
}

void GraphEditorComponent::resized() {
    statusLabel.setBounds(getLocalBounds().removeFromTop(20).reduced(8, 2));

    constexpr int kSearchWidth = 380, kSearchHeight = 320;
    searchPopup->setBounds(getLocalBounds().getCentreX() - kSearchWidth / 2, 40, kSearchWidth, kSearchHeight);
}

juce::Path GraphEditorComponent::buildConnectionPath(juce::Point<float> from, juce::Point<float> to) {
    juce::Path path;
    path.startNewSubPath(from);
    const float bend = juce::jmax(30.0f, std::abs(to.x - from.x) * 0.5f);
    path.cubicTo(from.x + bend, from.y, to.x - bend, to.y, to.x, to.y);
    return path;
}

void GraphEditorComponent::drawConnection(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to) {
    g.setColour(Palette::accent);
    g.strokePath(buildConnectionPath(from, to), juce::PathStrokeType(2.0f));
}

void GraphEditorComponent::paintConnections(juce::Graphics& g) {
    if (currentGraph == nullptr)
        return;

    for (const auto& n : currentGraph->nodes()) {
        auto dstIt = nodeComponentById.find(n.id);
        if (dstIt == nodeComponentById.end())
            continue;
        for (size_t p = 0; p < n.inputs.size(); ++p) {
            const auto& slot = n.inputs[p];
            if (slot.sourceNode == Graph::kInvalidNodeId)
                continue;
            auto srcIt = nodeComponentById.find(slot.sourceNode);
            if (srcIt == nodeComponentById.end())
                continue;
            drawConnection(g, srcIt->second->getOutputPinCentre(slot.sourceOutputPort).toFloat(),
                           dstIt->second->getInputPinCentre(static_cast<int>(p)).toFloat());
        }
    }

    if (isDraggingConnector) {
        auto it = nodeComponentById.find(dragSourceNode);
        if (it != nodeComponentById.end()) {
            // dragCurrentPos is screen space; paintConnections runs in canvas/world space - converted here.
            const auto dragCurrentInCanvas = canvas.getLocalPoint(this, dragCurrentPos.toInt()).toFloat();
            const auto fixedEnd = (dragSourceIsOutput ? it->second->getOutputPinCentre(dragSourcePort)
                                                        : it->second->getInputPinCentre(dragSourcePort))
                                       .toFloat();
            if (dragSourceIsOutput)
                drawConnection(g, fixedEnd, dragCurrentInCanvas);
            else
                drawConnection(g, dragCurrentInCanvas, fixedEnd);
        }
    }

    if (isMarqueeSelecting) {
        const juce::Rectangle<float> rect(marqueeStartCanvas, marqueeCurrentCanvas);
        g.setColour(Palette::accent.withAlpha(0.15f));
        g.fillRect(rect);
        g.setColour(Palette::accent);
        g.drawRect(rect, 1.0f);
    }
}

void GraphEditorComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::bg);
}
