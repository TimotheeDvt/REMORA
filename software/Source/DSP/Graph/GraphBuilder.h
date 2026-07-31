#pragma once

#include "NodeGraph.h"

// Thin helper for constructing a NodeGraph in C++, used by every Presets/*.cpp file.
namespace Graph {

class GraphBuilder {
public:
    explicit GraphBuilder(NodeGraph& graph) : graph_(graph) {}

    NodeId add(const juce::String& typeId, std::vector<float> params = {}) {
        return graph_.addNode(typeId, std::move(params));
    }

    // Wires output port 0 of `src` into `dst`'s input port `dstPort`.
    GraphBuilder& wire(NodeId src, NodeId dst, int dstPort = 0) {
        graph_.connect(src, 0, dst, dstPort);
        return *this;
    }

    // For multi-output nodes like source.spinClassification.
    GraphBuilder& wire(NodeId src, int srcPort, NodeId dst, int dstPort) {
        graph_.connect(src, srcPort, dst, dstPort);
        return *this;
    }

    GraphBuilder& setDefault(NodeId dst, int dstPort, float value) {
        graph_.setInputDefault(dst, dstPort, value);
        return *this;
    }

    NodeGraph& graph() { return graph_; }

private:
    NodeGraph& graph_;
};

} // namespace Graph
