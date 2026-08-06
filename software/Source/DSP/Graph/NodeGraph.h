#pragma once

#include "GraphTypes.h"
#include "NodeTypeRegistry.h"
#include <unordered_map>
#include <vector>

namespace Graph {

struct NodeInstance {
    NodeId id = kInvalidNodeId;
    juce::String typeId;
    std::vector<InputSlot> inputs;
    std::vector<float> params;
    std::unique_ptr<NodeState> state;   // null for stateless nodes
    mutable std::array<float, kMaxNodeOutputs> lastOutputs{};
    float x = 0.0f, y = 0.0f;            // canvas position, for the graph editor
    float w = 0.0f, h = 0.0f;            // canvas size override, for resizable display nodes (0 = use type default)
};

struct NodeCounts {
    int total = 0;
    int source = 0;
    int math = 0;
    int sink = 0;
    int display = 0;
};

// A DAG of typed nodes wired together, evaluated once per audio block
class NodeGraph {
public:
    NodeId addNode(const juce::String& typeId, std::vector<float> params = {});

    bool removeNode(NodeId id);

    // Returns false, leaving the graph unchanged, if this would create a cycle or reference an unknown node/port.
    bool connect(NodeId src, int srcPort, NodeId dst, int dstPort);

    bool disconnectInput(NodeId dst, int dstPort);

    void setInputDefault(NodeId dst, int dstPort, float value);
    void setNodePosition(NodeId id, float x, float y);
    void setNodeSize(NodeId id, float w, float h);

    void setNodeParams(NodeId id, std::vector<float> params);
    void setNodeParam(NodeId id, int index, float value);

    void prepare(double sampleRate);
    void evaluate(const SourceFrame& sources, MappingOutput& out);

    // Used by GraphMappingStrategy to feed the auto-registered MonitorParams.
    float outputOf(NodeId id, int port = 0) const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    static std::unique_ptr<NodeGraph> fromXml(const juce::XmlElement& xml);

    bool resetFromXml(const juce::XmlElement& xml);

    const std::vector<NodeInstance>& nodes() const { return nodes_; }

    NodeCounts countNodesByCategory() const;

private:
    std::vector<NodeInstance> nodes_;
    std::unordered_map<NodeId, size_t> indexById_;
    std::vector<NodeId> topoOrder_;
    NodeId nextId_ = 0;

    mutable juce::CriticalSection lock_;

    NodeInstance* addNodeWithId(NodeId id, const juce::String& typeId, std::vector<float> params);
    int indexOf(NodeId id) const;
    bool populateFromXml(const juce::XmlElement& xml);
    bool recomputeTopoOrder();
};

} // namespace Graph
