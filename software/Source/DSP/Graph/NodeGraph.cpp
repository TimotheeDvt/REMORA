#include "NodeGraph.h"
#include "../IMappingStrategy.h"

namespace Graph {

int NodeGraph::indexOf(NodeId id) const {
    auto it = indexById_.find(id);
    return it == indexById_.end() ? -1 : static_cast<int>(it->second);
}

NodeInstance* NodeGraph::addNodeWithId(NodeId id, const juce::String& typeId, std::vector<float> params) {
    const NodeTypeInfo* info = NodeTypeRegistry::instance().find(typeId);
    jassert(info != nullptr && "unknown node type id");

    NodeInstance inst;
    inst.id = id;
    inst.typeId = typeId;
    inst.inputs.resize(static_cast<size_t>(info->numInputs));
    inst.params = params.empty() ? info->defaultParams : std::move(params);
    if (info->isStateful && info->makeState)
        inst.state = info->makeState();

    indexById_[id] = nodes_.size();
    nodes_.push_back(std::move(inst));
    return &nodes_.back();
}

NodeId NodeGraph::addNode(const juce::String& typeId, std::vector<float> params) {
    const juce::ScopedLock sl(lock_);
    NodeId id = nextId_++;
    addNodeWithId(id, typeId, std::move(params));
    return id;
}

bool NodeGraph::removeNode(NodeId id) {
    const juce::ScopedLock sl(lock_);
    const int idx = indexOf(id);
    if (idx < 0)
        return false;

    for (auto& n : nodes_)
        for (auto& slot : n.inputs)
            if (slot.sourceNode == id)
                slot = InputSlot{ kInvalidNodeId, 0, slot.defaultValue };

    nodes_.erase(nodes_.begin() + idx);

    indexById_.clear();
    for (size_t i = 0; i < nodes_.size(); ++i)
        indexById_[nodes_[i].id] = i;

    recomputeTopoOrder();
    return true;
}

bool NodeGraph::disconnectInput(NodeId dst, int dstPort) {
    const juce::ScopedLock sl(lock_);
    const int dstIdx = indexOf(dst);
    if (dstIdx < 0)
        return false;
    NodeInstance& dstInst = nodes_[static_cast<size_t>(dstIdx)];
    if (dstPort < 0 || dstPort >= static_cast<int>(dstInst.inputs.size()))
        return false;

    InputSlot& slot = dstInst.inputs[static_cast<size_t>(dstPort)];
    slot.sourceNode = kInvalidNodeId;
    slot.sourceOutputPort = 0;
    recomputeTopoOrder();
    return true;
}

void NodeGraph::setNodePosition(NodeId id, float x, float y) {
    const juce::ScopedLock sl(lock_);
    const int idx = indexOf(id);
    if (idx < 0)
        return;
    nodes_[static_cast<size_t>(idx)].x = x;
    nodes_[static_cast<size_t>(idx)].y = y;
}

void NodeGraph::setNodeSize(NodeId id, float w, float h) {
    const juce::ScopedLock sl(lock_);
    const int idx = indexOf(id);
    if (idx < 0)
        return;
    nodes_[static_cast<size_t>(idx)].w = w;
    nodes_[static_cast<size_t>(idx)].h = h;
}

void NodeGraph::setNodeParams(NodeId id, std::vector<float> params) {
    const juce::ScopedLock sl(lock_);
    const int idx = indexOf(id);
    if (idx < 0)
        return;
    nodes_[static_cast<size_t>(idx)].params = std::move(params);
}

void NodeGraph::setNodeParam(NodeId id, int index, float value) {
    const juce::ScopedLock sl(lock_);
    const int idx = indexOf(id);
    if (idx < 0)
        return;
    auto& params = nodes_[static_cast<size_t>(idx)].params;
    if (index < 0 || index >= static_cast<int>(params.size()))
        return;
    params[static_cast<size_t>(index)] = value;
}

bool NodeGraph::connect(NodeId src, int srcPort, NodeId dst, int dstPort) {
    const juce::ScopedLock sl(lock_);
    int srcIdx = indexOf(src);
    int dstIdx = indexOf(dst);
    if (srcIdx < 0 || dstIdx < 0)
        return false;

    NodeInstance& dstInst = nodes_[static_cast<size_t>(dstIdx)];
    if (dstPort < 0 || dstPort >= static_cast<int>(dstInst.inputs.size()))
        return false;

    const InputSlot previous = dstInst.inputs[static_cast<size_t>(dstPort)];
    dstInst.inputs[static_cast<size_t>(dstPort)] = InputSlot{ src, srcPort, previous.defaultValue };

    if (recomputeTopoOrder())
        return true;

    // Revert - this connection would have created a cycle.
    dstInst.inputs[static_cast<size_t>(dstPort)] = previous;
    recomputeTopoOrder();
    return false;
}

void NodeGraph::setInputDefault(NodeId dst, int dstPort, float value) {
    const juce::ScopedLock sl(lock_);
    int dstIdx = indexOf(dst);
    if (dstIdx < 0)
        return;
    NodeInstance& dstInst = nodes_[static_cast<size_t>(dstIdx)];
    if (dstPort < 0 || dstPort >= static_cast<int>(dstInst.inputs.size()))
        return;
    dstInst.inputs[static_cast<size_t>(dstPort)].defaultValue = value;
}

bool NodeGraph::recomputeTopoOrder() {
    std::unordered_map<NodeId, int> inDegree;
    std::unordered_map<NodeId, std::vector<NodeId>> outEdges;
    inDegree.reserve(nodes_.size());

    for (const auto& n : nodes_)
        inDegree[n.id] = 0;

    for (const auto& n : nodes_) {
        for (const auto& slot : n.inputs) {
            if (slot.sourceNode != kInvalidNodeId) {
                outEdges[slot.sourceNode].push_back(n.id);
                inDegree[n.id]++;
            }
        }
    }

    std::vector<NodeId> queue;
    queue.reserve(nodes_.size());
    for (const auto& n : nodes_)
        if (inDegree[n.id] == 0)
            queue.push_back(n.id);

    std::vector<NodeId> order;
    order.reserve(nodes_.size());
    size_t qi = 0;
    while (qi < queue.size()) {
        NodeId current = queue[qi++];
        order.push_back(current);
        auto it = outEdges.find(current);
        if (it != outEdges.end()) {
            for (NodeId next : it->second)
                if (--inDegree[next] == 0)
                    queue.push_back(next);
        }
    }

    if (order.size() != nodes_.size())
        return false;

    topoOrder_ = std::move(order);
    return true;
}

void NodeGraph::prepare(double sampleRate) {
    const juce::ScopedLock sl(lock_);
    for (auto& n : nodes_)
        if (n.state)
            n.state->prepare(sampleRate);
    recomputeTopoOrder();
}

void NodeGraph::evaluate(const SourceFrame& sources, MappingOutput& out) {
    const juce::ScopedLock sl(lock_);
    const NodeTypeRegistry& registry = NodeTypeRegistry::instance();

    for (NodeId id : topoOrder_) {
        int idx = indexOf(id);
        if (idx < 0)
            continue;
        NodeInstance& n = nodes_[static_cast<size_t>(idx)];
        const NodeTypeInfo* info = registry.find(n.typeId);
        if (!info)
            continue;

        float inputValues[kMaxNodeInputs] = {};
        for (size_t p = 0; p < n.inputs.size() && p < static_cast<size_t>(kMaxNodeInputs); ++p) {
            const InputSlot& slot = n.inputs[p];
            if (slot.sourceNode == kInvalidNodeId) {
                inputValues[p] = slot.defaultValue;
                continue;
            }
            int si = indexOf(slot.sourceNode);
            inputValues[p] = (si >= 0)
                ? nodes_[static_cast<size_t>(si)].lastOutputs[static_cast<size_t>(slot.sourceOutputPort)]
                : slot.defaultValue;
        }

        switch (info->category) {
            case NodeCategory::Source:
                if (info->sourceEval)
                    info->sourceEval(sources, n.params, n.state.get(), n.lastOutputs.data());
                break;
            case NodeCategory::Math:
            case NodeCategory::Display: // performer-facing live displays are passthrough math nodes
                if (info->mathEval)
                    info->mathEval(inputValues, static_cast<int>(n.inputs.size()), n.params, n.state.get(), n.lastOutputs.data());
                break;
            case NodeCategory::Sink:
                if (info->sinkWrite)
                    info->sinkWrite(inputValues, n.params, out);
                // Sinks have no output of their own - stash the value that flowed into them so
                // liveOutputValue()/tooltips/monitor knobs can show "what's being sent to this sink".
                n.lastOutputs[0] = inputValues[0];
                break;
        }
    }
}

NodeCounts NodeGraph::countNodesByCategory() const {
    const juce::ScopedLock sl(lock_);
    const NodeTypeRegistry& registry = NodeTypeRegistry::instance();

    NodeCounts counts;
    for (const auto& n : nodes_) {
        const NodeTypeInfo* info = registry.find(n.typeId);
        if (info == nullptr)
            continue;

        ++counts.total;
        switch (info->category) {
            case NodeCategory::Source:  ++counts.source;  break;
            case NodeCategory::Math:    ++counts.math;    break;
            case NodeCategory::Sink:    ++counts.sink;    break;
            case NodeCategory::Display: ++counts.display; break;
        }
    }
    return counts;
}

float NodeGraph::outputOf(NodeId id, int port) const {
    const juce::ScopedLock sl(lock_);
    const int idx = indexOf(id);
    if (idx < 0)
        return 0.0f;
    const int clampedPort = juce::jlimit(0, kMaxNodeOutputs - 1, port);
    return nodes_[static_cast<size_t>(idx)].lastOutputs[static_cast<size_t>(clampedPort)];
}

std::unique_ptr<juce::XmlElement> NodeGraph::toXml() const {
    auto root = std::make_unique<juce::XmlElement>("NodeGraph");
    root->setAttribute("nextId", static_cast<int>(nextId_));

    for (const auto& n : nodes_) {
        auto* nodeXml = root->createNewChildElement("Node");
        nodeXml->setAttribute("id", static_cast<int>(n.id));
        nodeXml->setAttribute("type", n.typeId);
        nodeXml->setAttribute("x", n.x);
        nodeXml->setAttribute("y", n.y);
        if (n.w > 0.0f)
            nodeXml->setAttribute("w", n.w);
        if (n.h > 0.0f)
            nodeXml->setAttribute("h", n.h);

        for (size_t i = 0; i < n.params.size(); ++i) {
            auto* paramXml = nodeXml->createNewChildElement("Param");
            paramXml->setAttribute("index", static_cast<int>(i));
            paramXml->setAttribute("value", n.params[i]);
        }
        for (size_t p = 0; p < n.inputs.size(); ++p) {
            const InputSlot& slot = n.inputs[p];
            auto* inputXml = nodeXml->createNewChildElement("Input");
            inputXml->setAttribute("port", static_cast<int>(p));
            inputXml->setAttribute("sourceNode", static_cast<int>(slot.sourceNode));
            inputXml->setAttribute("sourceOutputPort", slot.sourceOutputPort);
            inputXml->setAttribute("default", slot.defaultValue);
        }
    }
    return root;
}

bool NodeGraph::populateFromXml(const juce::XmlElement& xml) {
    if (!xml.hasTagName("NodeGraph"))
        return false;

    for (auto* nodeXml : xml.getChildWithTagNameIterator("Node")) {
        const NodeId id = static_cast<NodeId>(nodeXml->getIntAttribute("id"));
        const juce::String typeId = nodeXml->getStringAttribute("type");
        if (NodeTypeRegistry::instance().find(typeId) == nullptr)
            return false; // malformed/unknown type

        std::vector<float> params;
        for (auto* paramXml : nodeXml->getChildWithTagNameIterator("Param")) {
            const size_t index = static_cast<size_t>(paramXml->getIntAttribute("index"));
            if (params.size() <= index)
                params.resize(index + 1, 0.0f);
            params[index] = static_cast<float>(paramXml->getDoubleAttribute("value"));
        }

        NodeInstance* inst = addNodeWithId(id, typeId, params);
        inst->x = static_cast<float>(nodeXml->getDoubleAttribute("x"));
        inst->y = static_cast<float>(nodeXml->getDoubleAttribute("y"));
        inst->w = static_cast<float>(nodeXml->getDoubleAttribute("w", 0.0));
        inst->h = static_cast<float>(nodeXml->getDoubleAttribute("h", 0.0));

        for (auto* inputXml : nodeXml->getChildWithTagNameIterator("Input")) {
            const size_t port = static_cast<size_t>(inputXml->getIntAttribute("port"));
            if (port >= inst->inputs.size())
                return false;
            InputSlot& slot = inst->inputs[port];
            slot.sourceNode = static_cast<NodeId>(inputXml->getIntAttribute("sourceNode", static_cast<int>(kInvalidNodeId)));
            slot.sourceOutputPort = inputXml->getIntAttribute("sourceOutputPort");
            slot.defaultValue = static_cast<float>(inputXml->getDoubleAttribute("default"));
        }
    }

    nextId_ = static_cast<NodeId>(xml.getIntAttribute("nextId", static_cast<int>(nextId_)));
    return recomputeTopoOrder(); // fails (returns false) if the xml describes a cycle
}

std::unique_ptr<NodeGraph> NodeGraph::fromXml(const juce::XmlElement& xml) {
    auto graph = std::make_unique<NodeGraph>();
    if (!graph->populateFromXml(xml))
        return nullptr;
    return graph;
}

bool NodeGraph::resetFromXml(const juce::XmlElement& xml) {
    const juce::ScopedLock sl(lock_);
    nodes_.clear();
    indexById_.clear();
    topoOrder_.clear();
    nextId_ = 0;
    const bool ok = populateFromXml(xml);
    jassert(ok); // xml is expected to be a snapshot this same graph produced earlier via toXml()
    return ok;
}

} // namespace Graph
