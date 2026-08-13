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
#include <limits>
#include <map>
#include <memory>
#include <utility>
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

void GraphEditorComponent::setClusterDisplays(bool shouldCluster) {
    if (clusterDisplays == shouldCluster)
        return;
    clusterDisplays = shouldCluster;
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

void GraphEditorComponent::autoLayout() {
    if (currentGraph == nullptr)
        return;

    const auto& nodes = currentGraph->nodes();
    if (nodes.empty())
        return;

    using Graph::NodeId;

    std::unordered_map<NodeId, bool> isDisplayCategory;
    for (const auto& n : nodes) {
        const Graph::NodeTypeInfo* info = Graph::NodeTypeRegistry::instance().find(n.typeId);
        isDisplayCategory[n.id] = info != nullptr && info->category == Graph::NodeCategory::Display;
    }
    const bool doCluster = clusterDisplays;
    auto isClustered = [&](NodeId id) { return doCluster && isDisplayCategory[id]; };

    std::unordered_map<NodeId, const Graph::NodeInstance*> nodeById;
    std::unordered_map<NodeId, std::vector<NodeId>> parentsOf, childrenOf;
    std::vector<NodeId> layoutIds;
    for (const auto& n : nodes) {
        nodeById[n.id] = &n;
        if (!isClustered(n.id)) {
            layoutIds.push_back(n.id);
            parentsOf[n.id];
            childrenOf[n.id];
        }
    }
    std::unordered_map<NodeId, std::unordered_map<NodeId, std::vector<int>>> inPortsFromParent;  // [child][parent]
    std::unordered_map<NodeId, std::unordered_map<NodeId, std::vector<int>>> outPortsToChild;     // [parent][child]
    for (const auto& n : nodes) {
        if (isClustered(n.id))
            continue;
        for (size_t portIdx = 0; portIdx < n.inputs.size(); ++portIdx) {
            const auto& slot = n.inputs[portIdx];
            if (slot.sourceNode == Graph::kInvalidNodeId || isClustered(slot.sourceNode))
                continue;
            parentsOf[n.id].push_back(slot.sourceNode);
            childrenOf[slot.sourceNode].push_back(n.id);
            inPortsFromParent[n.id][slot.sourceNode].push_back(static_cast<int>(portIdx));
            outPortsToChild[slot.sourceNode][n.id].push_back(slot.sourceOutputPort);
        }
    }
    for (auto& [id, v] : parentsOf) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }
    for (auto& [id, v] : childrenOf) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }

    std::unordered_map<NodeId, int> pendingParents;
    for (NodeId id : layoutIds)
        pendingParents[id] = static_cast<int>(parentsOf[id].size());

    std::vector<NodeId> ordered;
    ordered.reserve(layoutIds.size());
    {
        std::vector<NodeId> ready;
        for (NodeId id : layoutIds)
            if (pendingParents[id] == 0)
                ready.push_back(id);

        for (size_t head = 0; head < ready.size(); ++head) {
            const NodeId id = ready[head];
            ordered.push_back(id);
            for (NodeId child : childrenOf[id])
                if (--pendingParents[child] == 0)
                    ready.push_back(child);
        }

        if (ordered.size() < layoutIds.size()) {
            std::set<NodeId> seen(ordered.begin(), ordered.end());
            for (NodeId id : layoutIds)
                if (seen.count(id) == 0)
                    ordered.push_back(id);
        }
    }

    std::unordered_map<NodeId, int> rankOf;
    for (NodeId id : layoutIds)
        rankOf[id] = 0;
    for (NodeId id : ordered)
        for (NodeId child : childrenOf[id])
            rankOf[child] = std::max(rankOf[child], rankOf[id] + 1);

    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
        const auto& kids = childrenOf[*it];
        if (kids.empty())
            continue;
        int minChildRank = std::numeric_limits<int>::max();
        for (NodeId c : kids)
            minChildRank = std::min(minChildRank, rankOf[c]);
        rankOf[*it] = std::max(rankOf[*it], minChildRank - 1);
    }

    {
        std::set<int> distinctRanks;
        for (NodeId id : layoutIds)
            distinctRanks.insert(rankOf[id]);
        std::unordered_map<int, int> remap;
        int next = 0;
        for (int r : distinctRanks)
            remap[r] = next++;
        for (NodeId id : layoutIds)
            rankOf[id] = remap[rankOf[id]];
    }

    int maxRank = 0;
    for (NodeId id : layoutIds)
        maxRank = std::max(maxRank, rankOf[id]);

    std::unordered_map<NodeId, std::vector<NodeId>> chainParents = parentsOf, chainChildren = childrenOf;
    std::unordered_map<NodeId, int> layoutRank = rankOf;
    std::vector<NodeId> dummyIds;
    {
        NodeId nextDummyId = -2; // -1 is kInvalidNodeId
        for (const auto& [pid, kids] : childrenOf) {
            for (NodeId cid : kids) {
                const int rp = rankOf[pid];
                const int rc = rankOf[cid];
                NodeId prev = pid;
                for (int r = rp + 1; r < rc; ++r) {
                    const NodeId d = nextDummyId--;
                    dummyIds.push_back(d);
                    layoutRank[d] = r;
                    chainChildren[prev].push_back(d);
                    chainParents[d].push_back(prev);
                    prev = d;
                }
                if (prev != pid) {
                    auto& pc = chainChildren[pid];
                    pc.erase(std::remove(pc.begin(), pc.end(), cid), pc.end());
                    auto& cp = chainParents[cid];
                    cp.erase(std::remove(cp.begin(), cp.end(), pid), cp.end());
                    chainChildren[prev].push_back(cid);
                    chainParents[cid].push_back(prev);
                }
            }
        }
    }

    std::vector<std::vector<NodeId>> layers(static_cast<size_t>(maxRank) + 1);
    for (NodeId id : ordered)
        layers[static_cast<size_t>(rankOf[id])].push_back(id);
    for (NodeId d : dummyIds)
        layers[static_cast<size_t>(layoutRank[d])].push_back(d);

    std::unordered_map<NodeId, int> orderIndex;
    auto rebuildOrderIndex = [&]() {
        orderIndex.clear();
        for (const auto& layer : layers)
            for (size_t i = 0; i < layer.size(); ++i)
                orderIndex[layer[i]] = static_cast<int>(i);
    };
    rebuildOrderIndex();

    std::unordered_map<NodeId, int> numInputPorts, numOutputPorts;
    for (NodeId id : layoutIds) {
        const Graph::NodeInstance* n = nodeById[id];
        const Graph::NodeTypeInfo* info = n != nullptr ? Graph::NodeTypeRegistry::instance().find(n->typeId) : nullptr;
        numInputPorts[id] = n != nullptr ? std::max(1, static_cast<int>(n->inputs.size())) : 1;
        numOutputPorts[id] = info != nullptr ? std::max(1, info->numOutputs) : 1;
    }

    // Pixel y-offset (relative to the node's own top) of port `portIndex`, matching
    // GraphNodeComponent::resized()'s pin layout.
    auto portPixelY = [&](NodeId id, int portIndex) -> float {
        const Graph::NodeInstance* n = nodeById[id];
        if (n == nullptr)
            return 0.0f;
        const Graph::NodeTypeInfo* info = Graph::NodeTypeRegistry::instance().find(n->typeId);
        const float top = static_cast<float>(GraphNodeComponent::kHeaderHeight) +
                           static_cast<float>(n->params.size()) * static_cast<float>(GraphNodeComponent::kParamRowHeight);
        if (info != nullptr && info->displayKind != Graph::DisplayKind::None) {
            const float h = static_cast<float>(GraphNodeComponent::preferredHeight(*info, n->params.size()));
            return (top + h) * 0.5f;
        }
        return top + static_cast<float>(portIndex) * static_cast<float>(GraphNodeComponent::kRowHeight) +
               static_cast<float>(GraphNodeComponent::kRowHeight) * 0.5f;
    };

    auto fractionOf = [](int idx, int count) {
        return count > 1 ? static_cast<double>(idx) / static_cast<double>(count - 1) : 0.5;
    };

    // [child][parent] -> the input port(s) on `child` that connect to `parent`, as both a fraction
    // (0..1, for ordering) and an actual pixel offset (for y-placement). Averaged if more than one.
    std::unordered_map<NodeId, std::unordered_map<NodeId, double>> childPortFraction;
    std::unordered_map<NodeId, std::unordered_map<NodeId, float>> childPortY;
    for (const auto& [child, byParent] : inPortsFromParent)
        for (const auto& [parent, ports] : byParent) {
            double fracSum = 0.0;
            float ySum = 0.0f;
            for (int p : ports) {
                fracSum += fractionOf(p, numInputPorts[child]);
                ySum += portPixelY(child, p);
            }
            childPortFraction[child][parent] = fracSum / static_cast<double>(ports.size());
            childPortY[child][parent] = ySum / static_cast<float>(ports.size());
        }

    // [parent][child] -> same, for the output port(s) on `parent` used for edges to `child`.
    std::unordered_map<NodeId, std::unordered_map<NodeId, double>> parentPortFraction;
    std::unordered_map<NodeId, std::unordered_map<NodeId, float>> parentPortY;
    for (const auto& [parent, byChild] : outPortsToChild)
        for (const auto& [child, ports] : byChild) {
            double fracSum = 0.0;
            float ySum = 0.0f;
            for (int p : ports) {
                fracSum += fractionOf(p, numOutputPorts[parent]);
                ySum += portPixelY(parent, p);
            }
            parentPortFraction[parent][child] = fracSum / static_cast<double>(ports.size());
            parentPortY[parent][child] = ySum / static_cast<float>(ports.size());
        }

    // 0.5 (centre) fallback covers dummy nodes and dummy-chain hops, which have no real ports.
    auto orderingFraction = [&](NodeId neighbour, NodeId owner, bool neighbourIsParent) -> double {
        const auto& table = neighbourIsParent ? parentPortFraction : childPortFraction;
        auto it = table.find(neighbour);
        if (it == table.end())
            return 0.5;
        auto it2 = it->second.find(owner);
        return it2 != it->second.end() ? it2->second : 0.5;
    };

    auto medianOf = [&](NodeId owner, const std::vector<NodeId>& neighbours, bool neighboursAreParents) -> double {
        if (neighbours.empty())
            return -1.0;
        std::vector<double> pos;
        pos.reserve(neighbours.size());
        for (NodeId nb : neighbours)
            pos.push_back(orderIndex[nb] + orderingFraction(nb, owner, neighboursAreParents));
        std::sort(pos.begin(), pos.end());
        const size_t m = pos.size() / 2;
        if (pos.size() % 2 == 1)
            return pos[m];
        if (pos.size() == 2)
            return (pos[0] + pos[1]) * 0.5;
        const double left = pos[m - 1] - pos[0];
        const double right = pos.back() - pos[m];
        if (std::abs(left + right) < 1e-9)
            return (pos[m - 1] + pos[m]) * 0.5;
        return (pos[m - 1] * right + pos[m] * left) / (left + right);
    };

    auto crossingsBetween = [&](size_t upperLayer) -> long long {
        std::vector<std::pair<double, double>> edges;
        for (NodeId id : layers[upperLayer])
            for (NodeId child : chainChildren[id])
                if (layoutRank[child] == static_cast<int>(upperLayer) + 1)
                    edges.emplace_back(orderIndex[id] + orderingFraction(id, child, true),
                                        orderIndex[child] + orderingFraction(child, id, false));
        long long total = 0;
        for (size_t i = 0; i < edges.size(); ++i)
            for (size_t j = i + 1; j < edges.size(); ++j)
                if ((edges[i].first - edges[j].first) * (edges[i].second - edges[j].second) < 0)
                    ++total;
        return total;
    };

    auto totalCrossings = [&]() -> long long {
        long long total = 0;
        for (size_t r = 0; r + 1 < layers.size(); ++r)
            total += crossingsBetween(r);
        return total;
    };

    auto transposePass = [&]() {
        bool improved = true;
        int guard = 0;
        while (improved && guard++ < 4) {
            improved = false;
            for (size_t r = 0; r < layers.size(); ++r) {
                auto& layer = layers[r];
                for (size_t i = 0; i + 1 < layer.size(); ++i) {
                    const long long before = (r > 0 ? crossingsBetween(r - 1) : 0) + crossingsBetween(r);
                    std::swap(layer[i], layer[i + 1]);
                    std::swap(orderIndex[layer[i]], orderIndex[layer[i + 1]]);
                    const long long after = (r > 0 ? crossingsBetween(r - 1) : 0) + crossingsBetween(r);
                    if (after < before) {
                        improved = true;
                    } else {
                        std::swap(layer[i], layer[i + 1]);
                        std::swap(orderIndex[layer[i]], orderIndex[layer[i + 1]]);
                    }
                }
            }
        }
    };

    std::vector<std::vector<NodeId>> bestLayers = layers;
    long long bestCrossings = totalCrossings();

    constexpr int kSweeps = 8;
    for (int sweep = 0; sweep < kSweeps && bestCrossings > 0; ++sweep) {
        const bool downward = (sweep % 2 == 0);
        auto sweepLayer = [&](size_t r, const std::unordered_map<NodeId, std::vector<NodeId>>& neighbourMap,
                               size_t neighbourLayerSize, bool neighboursAreParents) {
            auto& layer = layers[r];
            const double neighbourScale = neighbourLayerSize > 1 ? static_cast<double>(neighbourLayerSize - 1) : 1.0;
            const double ownScale = layer.size() > 1 ? static_cast<double>(layer.size() - 1) : 1.0;
            std::vector<std::pair<double, NodeId>> keyed;
            keyed.reserve(layer.size());
            for (size_t i = 0; i < layer.size(); ++i) {
                const double med = medianOf(layer[i], neighbourMap.at(layer[i]), neighboursAreParents);
                const double key = med < 0.0 ? static_cast<double>(i) / ownScale : med / neighbourScale;
                keyed.emplace_back(key, layer[i]);
            }
            std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (size_t i = 0; i < layer.size(); ++i)
                layer[i] = keyed[i].second;
            rebuildOrderIndex();
        };

        if (downward) {
            for (size_t r = 1; r < layers.size(); ++r)
                sweepLayer(r, chainParents, layers[r - 1].size(), true);
        } else {
            for (size_t r = layers.size() - 1; r-- > 0;)
                sweepLayer(r, chainChildren, layers[r + 1].size(), false);
        }

        transposePass();

        const long long crossings = totalCrossings();
        if (crossings < bestCrossings) {
            bestCrossings = crossings;
            bestLayers = layers;
        }
    }
    layers = bestLayers;
    rebuildOrderIndex();

    std::unordered_map<NodeId, float> heightOf;
    for (NodeId id : layoutIds) {
        const Graph::NodeInstance* n = nodeById[id];
        const Graph::NodeTypeInfo* info = n != nullptr ? Graph::NodeTypeRegistry::instance().find(n->typeId) : nullptr;
        heightOf[id] = static_cast<float>(info != nullptr ? GraphNodeComponent::preferredHeight(*info, n->params.size())
                                                            : GraphNodeComponent::kHeaderHeight);
    }
    for (NodeId d : dummyIds)
        heightOf[d] = static_cast<float>(GraphNodeComponent::kRowHeight);

    const float rowGap = nodeSep;
    std::unordered_map<NodeId, float> yOf;

    auto isotonicPlace = [](const std::vector<double>& targets, const std::vector<double>& gaps) {
        const size_t n = targets.size();
        std::vector<double> offset(n, 0.0);
        for (size_t i = 1; i < n; ++i)
            offset[i] = offset[i - 1] + gaps[i - 1];

        struct Block { double sum; int count; };
        std::vector<Block> stack;
        for (size_t i = 0; i < n; ++i) {
            Block b{ targets[i] - offset[i], 1 };
            while (!stack.empty() && stack.back().sum / stack.back().count > b.sum / b.count) {
                b.sum += stack.back().sum;
                b.count += stack.back().count;
                stack.pop_back();
            }
            stack.push_back(b);
        }

        std::vector<double> result(n);
        size_t idx = 0;
        for (const auto& b : stack) {
            const double value = b.sum / b.count;
            for (int k = 0; k < b.count; ++k, ++idx)
                result[idx] = value + offset[idx];
        }
        return result;
    };

    for (auto& layer : layers) {
        float y = 0.0f;
        for (NodeId id : layer) {
            yOf[id] = y;
            y += heightOf[id] + rowGap;
        }
    }

    // 0.5*height (centre) fallback covers dummy nodes and dummy-chain hops, same as orderingFraction.
    auto parentOutY = [&](NodeId parent, NodeId child) -> float {
        auto it = parentPortY.find(parent);
        if (it != parentPortY.end()) {
            auto it2 = it->second.find(child);
            if (it2 != it->second.end())
                return it2->second;
        }
        return heightOf[parent] * 0.5f;
    };
    auto childInY = [&](NodeId child, NodeId parent) -> float {
        auto it = childPortY.find(child);
        if (it != childPortY.end()) {
            auto it2 = it->second.find(parent);
            if (it2 != it->second.end())
                return it2->second;
        }
        return heightOf[child] * 0.5f;
    };

    auto relaxLayer = [&](size_t r, const std::unordered_map<NodeId, std::vector<NodeId>>& neighbourMap,
                           bool neighboursAreParents) {
        auto& layer = layers[r];
        if (layer.empty())
            return;
        std::vector<double> targets(layer.size());
        for (size_t i = 0; i < layer.size(); ++i) {
            const NodeId id = layer[i];
            const auto& neighbours = neighbourMap.at(id);
            if (neighbours.empty()) {
                targets[i] = yOf[id];
            } else {
                double sum = 0.0;
                for (NodeId nb : neighbours) {
                    if (neighboursAreParents)
                        sum += (yOf[nb] + parentOutY(nb, id)) - childInY(id, nb);
                    else
                        sum += (yOf[nb] + childInY(nb, id)) - parentOutY(id, nb);
                }
                targets[i] = sum / static_cast<double>(neighbours.size());
            }
        }
        std::vector<double> gaps(layer.size() - 1);
        for (size_t i = 0; i + 1 < layer.size(); ++i)
            gaps[i] = heightOf[layer[i]] + rowGap;
        const std::vector<double> placed = isotonicPlace(targets, gaps);
        for (size_t i = 0; i < layer.size(); ++i)
            yOf[layer[i]] = static_cast<float>(placed[i]);
    };

    constexpr int kRelaxIterations = 6;
    for (int iter = 0; iter < kRelaxIterations; ++iter) {
        if (iter % 2 == 0) {
            for (size_t r = 0; r < layers.size(); ++r)
                relaxLayer(r, chainParents, true); // rank 0 has no parents, so it anchors this pass
        } else {
            for (size_t r = layers.size(); r-- > 0;)
                relaxLayer(r, chainChildren, false); // the last rank has no children, so it anchors this pass
        }
    }

    auto effectiveWidth = [&](NodeId id) -> float {
        const Graph::NodeInstance* n = nodeById[id];
        const Graph::NodeTypeInfo* info = n != nullptr ? Graph::NodeTypeRegistry::instance().find(n->typeId) : nullptr;
        if (n == nullptr || info == nullptr)
            return static_cast<float>(GraphNodeComponent::kWidth);
        if (info->displayKind != Graph::DisplayKind::None) {
            const float w = n->w > 0.0f ? n->w : info->displayDefaultWidth;
            return std::max(static_cast<float>(GraphNodeComponent::kMinDisplayWidth), w);
        }
        const bool compact = info->id == "math.constant" && info->defaultParams.size() == 1;
        const bool resizable = info->defaultWidth > 0.0f;
        const float defaultW = compact ? static_cast<float>(GraphNodeComponent::kCompactWidth)
                                        : (resizable ? info->defaultWidth : static_cast<float>(GraphNodeComponent::kWidth));
        return (resizable && n->w > 0.0f) ? n->w : defaultW;
    };

    constexpr float kExtraGapPerWire = 8.0f;
    std::vector<float> maxWidthByRank(static_cast<size_t>(maxRank) + 1, static_cast<float>(GraphNodeComponent::kWidth));
    std::vector<int> maxFanInByRank(static_cast<size_t>(maxRank) + 1, 0);
    for (NodeId id : layoutIds) {
        const Graph::NodeInstance* n = nodeById[id];
        int wireCount = 0;
        if (n != nullptr)
            for (const auto& slot : n->inputs)
                if (slot.sourceNode != Graph::kInvalidNodeId)
                    ++wireCount;
        const size_t rank = static_cast<size_t>(rankOf[id]);
        maxFanInByRank[rank] = std::max(maxFanInByRank[rank], wireCount);
        maxWidthByRank[rank] = std::max(maxWidthByRank[rank], effectiveWidth(id));
    }
    std::vector<float> xOfRank(static_cast<size_t>(maxRank) + 1, 0.0f);
    for (int r = 1; r <= maxRank; ++r) {
        const float extra = static_cast<float>(std::max(0, maxFanInByRank[static_cast<size_t>(r)] - 1)) * kExtraGapPerWire;
        xOfRank[static_cast<size_t>(r)] =
            xOfRank[static_cast<size_t>(r - 1)] + maxWidthByRank[static_cast<size_t>(r - 1)] + rankSep + extra;
    }
    for (NodeId id : layoutIds)
        currentGraph->setNodePosition(id, xOfRank[static_cast<size_t>(rankOf[id])], yOf[id]);

    std::vector<Graph::NodeId> displayIds;
    for (const auto& n : nodes)
        if (isClustered(n.id))
            displayIds.push_back(n.id);
    std::sort(displayIds.begin(), displayIds.end());

    if (!displayIds.empty()) {
        constexpr int kDisplayCols = 3;
        constexpr float kCellWidth = 240.0f;   // covers the widest default display (Scope, 220px) plus margin
        constexpr float kCellHeight = 160.0f;  // covers the tallest default display (Meter/Scope, 130px) plus margin
        const float clusterX0 =
            xOfRank[static_cast<size_t>(maxRank)] + maxWidthByRank[static_cast<size_t>(maxRank)] + rankSep;
        for (size_t i = 0; i < displayIds.size(); ++i) {
            const int col = static_cast<int>(i) % kDisplayCols;
            const int row = static_cast<int>(i) / kDisplayCols;
            currentGraph->setNodePosition(displayIds[i], clusterX0 + static_cast<float>(col) * kCellWidth,
                                           static_cast<float>(row) * kCellHeight);
        }
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
    const float bend = juce::jmax(30.0f, std::abs(to.x - from.x) * 0.5f);
    const juce::Rectangle<float> bounds(juce::jmin(from.x, to.x) - bend, juce::jmin(from.y, to.y) - 2.0f,
                                         std::abs(to.x - from.x) + bend * 2.0f, std::abs(to.y - from.y) + 4.0f);
    if (!g.getClipBounds().toFloat().intersects(bounds))
        return;

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
