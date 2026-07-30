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
};

// A DAG of typed nodes wired together, evaluated once per audio block. See
// Source/DSP/Graph/NodeTypeRegistry.h for the node catalog and
// /home/kadora/.claude/plans/vectorized-mapping-kahan.md for the design.
class NodeGraph {
public:
    NodeId addNode(const juce::String& typeId, std::vector<float> params = {});

    // Wires src's output port `srcPort` into dst's input port `dstPort`.
    // Returns false (and leaves the graph unchanged) if this would create a
    // cycle or reference an unknown node/port.
    bool connect(NodeId src, int srcPort, NodeId dst, int dstPort);

    void setInputDefault(NodeId dst, int dstPort, float value);

    void prepare(double sampleRate);
    void evaluate(const SourceFrame& sources, MappingOutput& out);

    // Last block's value at a given node's output port - used by
    // GraphMappingStrategy to feed the auto-registered MonitorParams.
    float outputOf(NodeId id, int port = 0) const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    static std::unique_ptr<NodeGraph> fromXml(const juce::XmlElement& xml);

    const std::vector<NodeInstance>& nodes() const { return nodes_; }

private:
    std::vector<NodeInstance> nodes_;
    std::unordered_map<NodeId, size_t> indexById_;
    std::vector<NodeId> topoOrder_;
    NodeId nextId_ = 0;

    NodeInstance* addNodeWithId(NodeId id, const juce::String& typeId, std::vector<float> params);
    int indexOf(NodeId id) const;
    bool recomputeTopoOrder();
};

} // namespace Graph
