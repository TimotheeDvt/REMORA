#include "GraphEditorComponent.h"
#include "../DSP/Graph/GraphMappingStrategy.h"
#include "../DSP/Graph/NodeTypeRegistry.h"
#include "../PluginProcessor.h"
#include "GraphPinComponent.h"
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
    styleLabel(statusLabel, {}, 13.0f, Palette::textMid, juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    canvas.setSize(kCanvasWidth, kCanvasHeight);
    canvas.setBufferedToImage(false);
    addAndMakeVisible(canvas);

    panOffset = { -kCanvasOriginX, -kCanvasOriginY };
    updateTransform();
}

void GraphEditorComponent::onMappingChanged() {
    auto* mapping = processor.getSynth().getMapping(processor.getMappingStrategy());
    auto* graphMapping = dynamic_cast<Graph::GraphMappingStrategy*>(mapping);
    currentGraph = graphMapping != nullptr ? &graphMapping->getGraph() : nullptr;

    const juce::String name = mapping != nullptr ? mapping->getName() : juce::String();
    isEditable = graphMapping != nullptr;

    statusLabel.setText(isEditable ? "Editing: " + name : "Viewing: " + name, juce::dontSendNotification);

    if (currentGraph != nullptr && autoLaidOutGraphs.insert(currentGraph).second) {
        autoLayout();
        originalSnapshots[currentGraph] = currentGraph->toXml()->toString();
    }

    isDirty = currentGraph != nullptr && dirtyByGraph.count(currentGraph) > 0 && dirtyByGraph[currentGraph];
    if (onDirtyStateChanged)
        onDirtyStateChanged(isDirty);

    nodeComponents.clear();
    nodeComponentById.clear();
    syncFromModel();
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

    nodeComponents.clear();
    nodeComponentById.clear();
    syncFromModel();
    canvas.repaint();
}

void GraphEditorComponent::autoLayout() {
    if (currentGraph == nullptr)
        return;

    std::unordered_map<Graph::NodeId, int> depthById;
    std::function<int(Graph::NodeId)> depthOf = [&](Graph::NodeId id) -> int {
        auto it = depthById.find(id);
        if (it != depthById.end())
            return it->second;
        depthById[id] = 0; // breaks any (unexpected) recursive lookup before it can loop forever
        int depth = 0;
        for (const auto& n : currentGraph->nodes()) {
            if (n.id != id)
                continue;
            for (const auto& slot : n.inputs)
                if (slot.sourceNode != Graph::kInvalidNodeId)
                    depth = juce::jmax(depth, depthOf(slot.sourceNode) + 1);
            break;
        }
        depthById[id] = depth;
        return depth;
    };

    std::unordered_map<Graph::NodeId, int> laneById;
    int nextLane = 0;
    std::function<int(Graph::NodeId)> laneOf = [&](Graph::NodeId id) -> int {
        auto it = laneById.find(id);
        if (it != laneById.end())
            return it->second;
        laneById[id] = -1;
        int lane = -1;
        for (const auto& n : currentGraph->nodes()) {
            if (n.id != id)
                continue;
            for (const auto& slot : n.inputs) {
                if (slot.sourceNode != Graph::kInvalidNodeId) {
                    lane = laneOf(slot.sourceNode);
                    break;
                }
            }
            break;
        }
        if (lane < 0)
            lane = nextLane++;
        laneById[id] = lane;
        return lane;
    };

    std::vector<Graph::NodeId> order;
    order.reserve(currentGraph->nodes().size());
    for (const auto& n : currentGraph->nodes()) {
        depthOf(n.id);
        laneOf(n.id);
        order.push_back(n.id);
    }
    std::stable_sort(order.begin(), order.end(), [&](Graph::NodeId a, Graph::NodeId b) {
        if (laneById[a] != laneById[b])
            return laneById[a] < laneById[b];
        return depthById[a] < depthById[b];
    });

    constexpr int columnWidth = 160, rowHeight = 130, marginX = 20, marginY = 20;
    std::unordered_map<int, int> nextFreeRowInColumn;
    for (Graph::NodeId id : order) {
        const int column = depthById[id];
        const int row = nextFreeRowInColumn[column]++;
        currentGraph->setNodePosition(id, static_cast<float>(marginX + column * columnWidth),
                                      static_cast<float>(marginY + row * rowHeight));
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
        auto* comp = nodeComponents.add(new GraphNodeComponent(*this, n.id, *info, n.params));
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

void GraphEditorComponent::showAddNodeMenu(juce::Point<int> position) {
    if (!isEditable || currentGraph == nullptr)
        return;

    auto typeIdByItemId = std::make_shared<std::vector<juce::String>>();
    typeIdByItemId->push_back({}); // item id 0 is "dismissed", unused

    // category -> subcategory -> leaf items, so e.g. Source splits into "Raw Sensor"/"Derived Motion".
    std::map<Graph::NodeCategory, std::map<juce::String, juce::PopupMenu>> menusByCategory;

    for (const auto& info : Graph::NodeTypeRegistry::instance().all()) {
        typeIdByItemId->push_back(info.id);
        const int itemId = static_cast<int>(typeIdByItemId->size() - 1);
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

void GraphEditorComponent::deleteNode(Graph::NodeId id) {
    if (currentGraph == nullptr)
        return;
    currentGraph->removeNode(id);
    markDirty();
    syncFromModel();
}

void GraphEditorComponent::nodeMoved(Graph::NodeId id, float x, float y) {
    if (currentGraph == nullptr)
        return;
    currentGraph->setNodePosition(id, x - kCanvasOriginX, y - kCanvasOriginY);
    fixupOrderingAround(id);
    markDirty();
    canvas.repaint();
}

void GraphEditorComponent::showNodeContextMenu(Graph::NodeId id) {
    if (!isEditable)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Delete Node");
    menu.showMenuAsync(juce::PopupMenu::Options{}, [this, id](int result) {
        if (result == 1)
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

    juce::PopupMenu menu;
    menu.addItem(1, "Remove Connection");
    menu.showMenuAsync(juce::PopupMenu::Options{}, [this, dstNode, dstPort](int result) {
        if (result == 1 && currentGraph != nullptr) {
            currentGraph->disconnectInput(dstNode, dstPort);
            markDirty();
            canvas.repaint();
        }
    });
}

void GraphEditorComponent::mouseDown(const juce::MouseEvent& e) {
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
    // Plain click+drag on empty canvas pans - no modifier key needed.
    handleCanvasMouseDown(e);
}

void GraphEditorComponent::handleBackgroundMouseDrag(const juce::MouseEvent& e) {
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

void GraphEditorComponent::resized() {
    statusLabel.setBounds(getLocalBounds().removeFromTop(20).reduced(8, 2));
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
}

void GraphEditorComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::bg);
}
