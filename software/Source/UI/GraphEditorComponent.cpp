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
#include <memory>
#include <vector>

namespace {
constexpr int kCanvasWidth = 22000;
constexpr int kCanvasHeight = 22000;
constexpr float kMinZoom = 0.1f, kMaxZoom = 3.0f;
} // namespace

GraphEditorComponent::GraphEditorComponent(REMORAProcessor& p) : processor(p) {
    styleLabel(statusLabel, {}, 13.0f, Palette::textMid, juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    canvas.setSize(kCanvasWidth, kCanvasHeight);
    canvas.setBufferedToImage(false);
    addAndMakeVisible(canvas);
}

void GraphEditorComponent::onMappingChanged() {
    auto* mapping = processor.getSynth().getMapping(processor.getMappingStrategy());
    auto* graphMapping = dynamic_cast<Graph::GraphMappingStrategy*>(mapping);
    currentGraph = graphMapping != nullptr ? &graphMapping->getGraph() : nullptr;

    const juce::String name = mapping != nullptr ? mapping->getName() : juce::String();
    isEditable = graphMapping != nullptr && name == "Custom";

    statusLabel.setText(isEditable ? "Editing: Custom"
                                    : "Viewing: " + name + " (read-only - select Custom to edit)",
                         juce::dontSendNotification);

    if (currentGraph != nullptr && autoLaidOutGraphs.insert(currentGraph).second)
        autoLayout();

    nodeComponents.clear();
    nodeComponentById.clear();
    syncFromModel();
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

    std::unordered_map<Graph::NodeId, int> rowById;
    int nextRow = 0;
    std::function<int(Graph::NodeId)> rowOf = [&](Graph::NodeId id) -> int {
        auto it = rowById.find(id);
        if (it != rowById.end())
            return it->second;
        rowById[id] = -1; // breaks any (unexpected) recursive lookup before it can loop forever
        int row = -1;
        for (const auto& n : currentGraph->nodes()) {
            if (n.id != id)
                continue;
            for (const auto& slot : n.inputs) {
                if (slot.sourceNode != Graph::kInvalidNodeId) {
                    row = rowOf(slot.sourceNode);
                    break;
                }
            }
            break;
        }
        if (row < 0)
            row = nextRow++;
        rowById[id] = row;
        return row;
    };

    for (const auto& n : currentGraph->nodes())
        rowOf(n.id);

    std::vector<std::vector<int>> rowAdjacency(static_cast<size_t>(nextRow));
    for (const auto& n : currentGraph->nodes()) {
        const int nodeRow = rowOf(n.id);
        for (const auto& slot : n.inputs) {
            if (slot.sourceNode == Graph::kInvalidNodeId)
                continue;
            const int otherRow = rowOf(slot.sourceNode);
            if (otherRow != nodeRow) {
                rowAdjacency[static_cast<size_t>(nodeRow)].push_back(otherRow);
                rowAdjacency[static_cast<size_t>(otherRow)].push_back(nodeRow);
            }
        }
    }

    std::vector<int> finalRowPosition(static_cast<size_t>(nextRow), -1);
    int nextPosition = 0;
    for (int startRow = 0; startRow < nextRow; ++startRow) {
        if (finalRowPosition[static_cast<size_t>(startRow)] >= 0)
            continue;
        std::vector<int> queue{ startRow };
        finalRowPosition[static_cast<size_t>(startRow)] = nextPosition++;
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            for (int neighbour : rowAdjacency[static_cast<size_t>(queue[qi])]) {
                if (finalRowPosition[static_cast<size_t>(neighbour)] < 0) {
                    finalRowPosition[static_cast<size_t>(neighbour)] = nextPosition++;
                    queue.push_back(neighbour);
                }
            }
        }
    }

    constexpr int columnWidth = 160, rowHeight = 130, marginX = 20, marginY = 20;
    std::unordered_map<int, int> nextFreeColumnInRow;
    for (const auto& n : currentGraph->nodes()) {
        const int row = rowOf(n.id);
        const int column = juce::jmax(depthOf(n.id), nextFreeColumnInRow[row]);
        nextFreeColumnInRow[row] = column + 1;

        currentGraph->setNodePosition(n.id, static_cast<float>(marginX + column * columnWidth),
                                      static_cast<float>(marginY + finalRowPosition[static_cast<size_t>(row)] * rowHeight));
    }
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
        comp->setTopLeftPosition(static_cast<int>(n.x), static_cast<int>(n.y));
        canvas.addAndMakeVisible(comp);
        nodeComponentById[n.id] = comp;
    }

    canvas.repaint();
}

void GraphEditorComponent::showAddNodeMenu(juce::Point<int> position) {
    if (!isEditable || currentGraph == nullptr)
        return;

    juce::PopupMenu sourceMenu, mathMenu, sinkMenu;
    auto typeIdByItemId = std::make_shared<std::vector<juce::String>>();
    typeIdByItemId->push_back({}); // item id 0 is "dismissed", unused

    for (const auto& info : Graph::NodeTypeRegistry::instance().all()) {
        typeIdByItemId->push_back(info.id);
        const int itemId = static_cast<int>(typeIdByItemId->size() - 1);
        switch (info.category) {
            case Graph::NodeCategory::Source: sourceMenu.addItem(itemId, info.displayName); break;
            case Graph::NodeCategory::Math:   mathMenu.addItem(itemId, info.displayName); break;
            case Graph::NodeCategory::Sink:   sinkMenu.addItem(itemId, info.displayName); break;
        }
    }

    juce::PopupMenu root;
    root.addSubMenu("Source", sourceMenu);
    root.addSubMenu("Math", mathMenu);
    root.addSubMenu("Sink", sinkMenu);

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
    const auto worldPos = canvas.getLocalPoint(this, position);
    const Graph::NodeId id = currentGraph->addNode(typeId);
    currentGraph->setNodePosition(id, static_cast<float>(worldPos.x), static_cast<float>(worldPos.y));
    syncFromModel();
}

void GraphEditorComponent::deleteNode(Graph::NodeId id) {
    if (currentGraph == nullptr)
        return;
    currentGraph->removeNode(id);
    syncFromModel();
}

void GraphEditorComponent::nodeMoved(Graph::NodeId id, float x, float y) {
    if (currentGraph == nullptr)
        return;
    currentGraph->setNodePosition(id, x, y);
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
                if (dragSourceIsOutput)
                    currentGraph->connect(dragSourceNode, dragSourcePort, targetPin->getNodeId(), targetPin->getPort());
                else
                    currentGraph->connect(targetPin->getNodeId(), targetPin->getPort(), dragSourceNode, dragSourcePort);
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

void GraphEditorComponent::mouseDown(const juce::MouseEvent& e) {
    handleBackgroundMouseDown(e);
}

void GraphEditorComponent::mouseDrag(const juce::MouseEvent& e) {
    handleBackgroundMouseDrag(e);
}

void GraphEditorComponent::handleBackgroundMouseDown(const juce::MouseEvent& e) {
    if (e.mods.isCtrlDown())
        handleCanvasMouseDown(e);
    else if (e.mods.isPopupMenu())
        showAddNodeMenu(e.getPosition());
}

void GraphEditorComponent::handleBackgroundMouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isCtrlDown())
        handleCanvasMouseDrag(e);
}

void GraphEditorComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    const float oldZoom = zoom;
    const float newZoom = juce::jlimit(kMinZoom, kMaxZoom, zoom * std::exp(wheel.deltaY * 2.0f));
    if (std::abs(newZoom - oldZoom) < 1.0e-6f)
        return;

    // Keep the world point currently under the cursor fixed on screen,
    // rather than always zooming toward the canvas origin.
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

void GraphEditorComponent::drawConnection(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to) {
    juce::Path path;
    path.startNewSubPath(from);
    const float bend = juce::jmax(30.0f, std::abs(to.x - from.x) * 0.5f);
    path.cubicTo(from.x + bend, from.y, to.x - bend, to.y, to.x, to.y);
    g.setColour(Palette::accent);
    g.strokePath(path, juce::PathStrokeType(2.0f));
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
            // dragCurrentPos is tracked in editor/screen space (see the
            // header comment); paintConnections runs in canvas/world space,
            // so it's converted here, at the point the two meet.
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
