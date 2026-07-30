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
    NodeId id = nextId_++;
    addNodeWithId(id, typeId, std::move(params));
    return id;
}

bool NodeGraph::connect(NodeId src, int srcPort, NodeId dst, int dstPort) {
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
    for (auto& n : nodes_)
        if (n.state)
            n.state->prepare(sampleRate);
    recomputeTopoOrder();
}

void NodeGraph::evaluate(const SourceFrame& sources, MappingOutput& out) {
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
                if (info->mathEval)
                    info->mathEval(inputValues, static_cast<int>(n.inputs.size()), n.params, n.state.get(), n.lastOutputs.data());
                break;
            case NodeCategory::Sink:
                if (info->sinkWrite)
                    info->sinkWrite(inputValues, n.params, out);
                break;
        }
    }
}

float NodeGraph::outputOf(NodeId id, int port) const {
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

std::unique_ptr<NodeGraph> NodeGraph::fromXml(const juce::XmlElement& xml) {
    if (!xml.hasTagName("NodeGraph"))
        return nullptr;

    auto graph = std::make_unique<NodeGraph>();

    for (auto* nodeXml : xml.getChildWithTagNameIterator("Node")) {
        const NodeId id = static_cast<NodeId>(nodeXml->getIntAttribute("id"));
        const juce::String typeId = nodeXml->getStringAttribute("type");
        if (NodeTypeRegistry::instance().find(typeId) == nullptr)
            return nullptr; // malformed/unknown type

        std::vector<float> params;
        for (auto* paramXml : nodeXml->getChildWithTagNameIterator("Param")) {
            const size_t index = static_cast<size_t>(paramXml->getIntAttribute("index"));
            if (params.size() <= index)
                params.resize(index + 1, 0.0f);
            params[index] = static_cast<float>(paramXml->getDoubleAttribute("value"));
        }

        NodeInstance* inst = graph->addNodeWithId(id, typeId, params);

        for (auto* inputXml : nodeXml->getChildWithTagNameIterator("Input")) {
            const size_t port = static_cast<size_t>(inputXml->getIntAttribute("port"));
            if (port >= inst->inputs.size())
                return nullptr;
            InputSlot& slot = inst->inputs[port];
            slot.sourceNode = static_cast<NodeId>(inputXml->getIntAttribute("sourceNode", static_cast<int>(kInvalidNodeId)));
            slot.sourceOutputPort = inputXml->getIntAttribute("sourceOutputPort");
            slot.defaultValue = static_cast<float>(inputXml->getDoubleAttribute("default"));
        }
    }

    graph->nextId_ = static_cast<NodeId>(xml.getIntAttribute("nextId", static_cast<int>(graph->nextId_)));
    if (!graph->recomputeTopoOrder())
        return nullptr; // malformed - contains a cycle

    return graph;
}

} // namespace Graph
