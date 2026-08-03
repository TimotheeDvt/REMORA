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

void GraphEditorComponent::autoLayout() {
    if (currentGraph == nullptr)
        return;

    const auto& nodes = currentGraph->nodes();
    if (nodes.empty())
        return;

    const Graph::NodeTypeRegistry& registry = Graph::NodeTypeRegistry::instance();
    auto heightOf = [&](Graph::NodeId id) -> int {
        const Graph::NodeInstance* n = findNode(id);
        const Graph::NodeTypeInfo* info = n != nullptr ? registry.find(n->typeId) : nullptr;
        return info != nullptr ? GraphNodeComponent::preferredHeight(*info) : GraphNodeComponent::kHeaderHeight;
    };

    std::unordered_map<Graph::NodeId, std::vector<Graph::NodeId>> predecessors, successors;
    for (const auto& n : nodes)
        for (const auto& slot : n.inputs)
            if (slot.sourceNode != Graph::kInvalidNodeId) {
                predecessors[n.id].push_back(slot.sourceNode);
                successors[slot.sourceNode].push_back(n.id);
            }

    // --- Phase 1: layering (longest path from any source node) ---
    std::unordered_map<Graph::NodeId, int> layerOf;
    std::function<int(Graph::NodeId)> computeLayer = [&](Graph::NodeId id) -> int {
        auto it = layerOf.find(id);
        if (it != layerOf.end())
            return it->second;
        layerOf[id] = 0; // guards against (unexpected) recursive lookup before it can loop forever
        int layer = 0;
        for (Graph::NodeId pred : predecessors[id])
            layer = juce::jmax(layer, computeLayer(pred) + 1);
        layerOf[id] = layer;
        return layer;
    };
    for (const auto& n : nodes)
        computeLayer(n.id);

    // A bare constant feeding exactly one node has no meaningful layer of its own - pull it into
    // the layer right before its consumer so it hugs it instead of stretching a wire across the
    // whole graph from an empty layer 0. Constants with multiple consumers keep their natural layer.
    std::unordered_map<Graph::NodeId, int> fanOutCount;
    for (const auto& [id, outs] : successors)
        fanOutCount[id] = static_cast<int>(outs.size());
    for (const auto& n : nodes) {
        if (n.typeId != "math.constant" || !n.inputs.empty() || fanOutCount[n.id] != 1)
            continue;
        const Graph::NodeId consumer = successors[n.id].front();
        layerOf[n.id] = juce::jmax(0, layerOf[consumer] - 1);
    }

    int numLayers = 0;
    for (const auto& n : nodes)
        numLayers = juce::jmax(numLayers, layerOf[n.id] + 1);

    // --- connected components, laid out as independent stacked blocks ---
    std::unordered_map<Graph::NodeId, Graph::NodeId> unionParent;
    for (const auto& n : nodes)
        unionParent[n.id] = n.id;
    std::function<Graph::NodeId(Graph::NodeId)> find = [&](Graph::NodeId id) {
        while (unionParent[id] != id) {
            unionParent[id] = unionParent[unionParent[id]];
            id = unionParent[id];
        }
        return id;
    };
    auto unite = [&](Graph::NodeId a, Graph::NodeId b) {
        a = find(a);
        b = find(b);
        if (a != b)
            unionParent[a] = b;
    };
    for (const auto& [id, preds] : predecessors)
        for (Graph::NodeId p : preds)
            unite(id, p);

    std::vector<Graph::NodeId> componentOrder;
    std::unordered_map<Graph::NodeId, std::vector<Graph::NodeId>> componentMembers;
    for (const auto& n : nodes) {
        const Graph::NodeId root = find(n.id);
        if (componentMembers.count(root) == 0)
            componentOrder.push_back(root);
        componentMembers[root].push_back(n.id);
    }

    constexpr float columnGap = static_cast<float>(GraphNodeComponent::kWidth) + 120.0f;
    constexpr float marginX = 40.0f, marginY = 20.0f, rowGap = 24.0f, blockGap = 60.0f;

    float blockTop = marginY;
    for (Graph::NodeId root : componentOrder) {
        const std::vector<Graph::NodeId>& members = componentMembers[root];

        std::vector<std::vector<Graph::NodeId>> layers(static_cast<size_t>(numLayers));
        for (Graph::NodeId id : members)
            layers[static_cast<size_t>(layerOf[id])].push_back(id);

        std::unordered_map<Graph::NodeId, float> orderInLayer;
        auto reindex = [&](std::vector<Graph::NodeId>& layer) {
            for (size_t r = 0; r < layer.size(); ++r)
                orderInLayer[layer[r]] = static_cast<float>(r);
        };
        for (auto& layer : layers)
            reindex(layer);

        // --- Phase 2: crossing reduction - alternating median/barycenter sweeps down and up the
        // layers, pulling each node toward the average position of its already-placed neighbours...
        auto barycenter = [&](Graph::NodeId id,
                               const std::unordered_map<Graph::NodeId, std::vector<Graph::NodeId>>& neighboursOf) {
            auto it = neighboursOf.find(id);
            if (it == neighboursOf.end() || it->second.empty())
                return orderInLayer[id]; // no anchor in the adjacent layer - hold position
            float sum = 0.0f;
            for (Graph::NodeId nb : it->second)
                sum += orderInLayer[nb];
            return sum / static_cast<float>(it->second.size());
        };

        constexpr int kSweeps = 8;
        for (int sweep = 0; sweep < kSweeps; ++sweep) {
            if (sweep % 2 == 0) {
                for (int l = 1; l < numLayers; ++l) {
                    auto& layer = layers[static_cast<size_t>(l)];
                    std::stable_sort(layer.begin(), layer.end(), [&](Graph::NodeId a, Graph::NodeId b) {
                        return barycenter(a, predecessors) < barycenter(b, predecessors);
                    });
                    reindex(layer);
                }
            } else {
                for (int l = numLayers - 2; l >= 0; --l) {
                    auto& layer = layers[static_cast<size_t>(l)];
                    std::stable_sort(layer.begin(), layer.end(), [&](Graph::NodeId a, Graph::NodeId b) {
                        return barycenter(a, successors) < barycenter(b, successors);
                    });
                    reindex(layer);
                }
            }
        }

        // ... then a few passes of adjacent transposition, swapping neighbours whenever it locally
        // reduces edge crossings against both adjacent layers.
        auto crossingsBetween = [&](Graph::NodeId left, Graph::NodeId right) {
            int crossings = 0;
            for (const auto* neighboursOf : { &predecessors, &successors }) {
                auto leftIt = neighboursOf->find(left);
                auto rightIt = neighboursOf->find(right);
                if (leftIt == neighboursOf->end() || rightIt == neighboursOf->end())
                    continue;
                for (Graph::NodeId a : leftIt->second)
                    for (Graph::NodeId b : rightIt->second)
                        if (orderInLayer[a] > orderInLayer[b])
                            ++crossings;
            }
            return crossings;
        };

        constexpr int kTransposePasses = 4;
        for (int pass = 0; pass < kTransposePasses; ++pass) {
            bool improved = false;
            for (auto& layer : layers) {
                for (size_t r = 0; r + 1 < layer.size(); ++r) {
                    if (crossingsBetween(layer[r], layer[r + 1]) > crossingsBetween(layer[r + 1], layer[r])) {
                        std::swap(layer[r], layer[r + 1]);
                        orderInLayer[layer[r]] = static_cast<float>(r);
                        orderInLayer[layer[r + 1]] = static_cast<float>(r + 1);
                        improved = true;
                    }
                }
            }
            if (!improved)
                break;
        }

        // --- Phase 3: coordinate assignment - stack each column by the order just computed, then
        // nudge nodes toward the average y of their neighbours to straighten edges, never past the
        // previous node in the same column so nothing overlaps. ---
        std::vector<float> columnBottom(static_cast<size_t>(numLayers), blockTop);
        for (int l = 0; l < numLayers; ++l) {
            const float x = marginX + static_cast<float>(l) * columnGap;
            for (Graph::NodeId id : layers[static_cast<size_t>(l)]) {
                const float y = columnBottom[static_cast<size_t>(l)];
                currentGraph->setNodePosition(id, x, y);
                columnBottom[static_cast<size_t>(l)] = y + static_cast<float>(heightOf(id)) + rowGap;
            }
        }

        constexpr int kAlignPasses = 4;
        for (int pass = 0; pass < kAlignPasses; ++pass) {
            for (int l = 0; l < numLayers; ++l) {
                const float x = marginX + static_cast<float>(l) * columnGap;
                float minY = blockTop;
                for (Graph::NodeId id : layers[static_cast<size_t>(l)]) {
                    float sum = 0.0f;
                    int count = 0;
                    for (const auto* neighboursOf : { &predecessors, &successors }) {
                        auto it = neighboursOf->find(id);
                        if (it == neighboursOf->end())
                            continue;
                        for (Graph::NodeId nb : it->second)
                            if (const Graph::NodeInstance* n = findNode(nb)) {
                                sum += n->y;
                                ++count;
                            }
                    }
                    const Graph::NodeInstance* self = findNode(id);
                    const float target =
                        count > 0 ? sum / static_cast<float>(count) : (self != nullptr ? self->y : minY);
                    const float y = juce::jmax(target, minY);
                    currentGraph->setNodePosition(id, x, y);
                    minY = y + static_cast<float>(heightOf(id)) + rowGap;
                }
            }
        }

        float blockBottom = blockTop;
        for (Graph::NodeId id : members)
            if (const Graph::NodeInstance* n = findNode(id))
                blockBottom = juce::jmax(blockBottom, n->y + static_cast<float>(heightOf(id)));
        blockTop = blockBottom + blockGap;
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
