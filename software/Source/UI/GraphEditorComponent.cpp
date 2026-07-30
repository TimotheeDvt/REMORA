#include "GraphEditorComponent.h"
#include "../DSP/Graph/NodeTypeRegistry.h"
#include "GraphPinComponent.h"
#include "Palette.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

GraphEditorComponent::GraphEditorComponent(REMORAProcessor& p)
    : processor(p), graph(std::make_unique<Graph::NodeGraph>()) {
}

void GraphEditorComponent::syncFromModel() {
    for (int i = nodeComponents.size(); --i >= 0;) {
        auto* comp = nodeComponents.getUnchecked(i);
        const bool stillExists = std::any_of(graph->nodes().begin(), graph->nodes().end(),
            [comp](const Graph::NodeInstance& n) { return n.id == comp->getNodeId(); });
        if (!stillExists) {
            nodeComponentById.erase(comp->getNodeId());
            nodeComponents.remove(i);
        }
    }

    for (const auto& n : graph->nodes()) {
        if (nodeComponentById.count(n.id) > 0)
            continue;
        const Graph::NodeTypeInfo* info = Graph::NodeTypeRegistry::instance().find(n.typeId);
        if (info == nullptr)
            continue;
        auto* comp = nodeComponents.add(new GraphNodeComponent(*this, n.id, *info));
        comp->setTopLeftPosition(static_cast<int>(n.x), static_cast<int>(n.y));
        addAndMakeVisible(comp);
        nodeComponentById[n.id] = comp;
    }

    repaint();
}

void GraphEditorComponent::showAddNodeMenu(juce::Point<int> position) {
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
    const Graph::NodeId id = graph->addNode(typeId);
    graph->setNodePosition(id, static_cast<float>(position.x), static_cast<float>(position.y));
    syncFromModel();
}

void GraphEditorComponent::deleteNode(Graph::NodeId id) {
    graph->removeNode(id);
    syncFromModel();
}

void GraphEditorComponent::nodeMoved(Graph::NodeId id, float x, float y) {
    graph->setNodePosition(id, x, y);
    repaint();
}

void GraphEditorComponent::showNodeContextMenu(Graph::NodeId id) {
    juce::PopupMenu menu;
    menu.addItem(1, "Delete Node");
    menu.showMenuAsync(juce::PopupMenu::Options{}, [this, id](int result) {
        if (result == 1)
            deleteNode(id);
    });
}

void GraphEditorComponent::showPinContextMenu(GraphPinComponent& pin) {
    if (pin.isOutputPin())
        return;

    bool connected = false;
    for (const auto& n : graph->nodes()) {
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
        if (result == 1) {
            graph->disconnectInput(nodeId, port);
            repaint();
        }
    });
}

void GraphEditorComponent::handlePinMouseDown(GraphPinComponent& pin, const juce::MouseEvent& e) {
    isDraggingConnector = true;
    dragSourceNode = pin.getNodeId();
    dragSourcePort = pin.getPort();
    dragSourceIsOutput = pin.isOutputPin();
    dragCurrentPos = e.position;
    repaint();
}

void GraphEditorComponent::handlePinMouseDrag(const juce::MouseEvent& e) {
    if (!isDraggingConnector)
        return;
    dragCurrentPos = e.position;
    repaint();
}

void GraphEditorComponent::handlePinMouseUp(const juce::MouseEvent& e) {
    if (!isDraggingConnector)
        return;
    isDraggingConnector = false;

    if (auto* targetPin = findPinAt(e.getPosition())) {
        if (targetPin->getNodeId() != dragSourceNode && targetPin->isOutputPin() != dragSourceIsOutput) {
            if (dragSourceIsOutput)
                graph->connect(dragSourceNode, dragSourcePort, targetPin->getNodeId(), targetPin->getPort());
            else
                graph->connect(targetPin->getNodeId(), targetPin->getPort(), dragSourceNode, dragSourcePort);
        }
    }
    repaint();
}

GraphPinComponent* GraphEditorComponent::findPinAt(juce::Point<int> posInEditor) const {
    for (auto* node : nodeComponents) {
        auto local = posInEditor - node->getPosition();
        if (auto* comp = node->getComponentAt(local))
            if (auto* pin = dynamic_cast<GraphPinComponent*>(comp))
                return pin;
    }
    return nullptr;
}

void GraphEditorComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        showAddNodeMenu(e.getPosition());
}

void GraphEditorComponent::drawConnection(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to) {
    juce::Path path;
    path.startNewSubPath(from);
    const float bend = juce::jmax(30.0f, std::abs(to.x - from.x) * 0.5f);
    path.cubicTo(from.x + bend, from.y, to.x - bend, to.y, to.x, to.y);
    g.setColour(Palette::accent);
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

void GraphEditorComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::bg);

    for (const auto& n : graph->nodes()) {
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
            const auto fixedEnd = (dragSourceIsOutput ? it->second->getOutputPinCentre(dragSourcePort)
                                                        : it->second->getInputPinCentre(dragSourcePort))
                                       .toFloat();
            if (dragSourceIsOutput)
                drawConnection(g, fixedEnd, dragCurrentPos);
            else
                drawConnection(g, dragCurrentPos, fixedEnd);
        }
    }
}
