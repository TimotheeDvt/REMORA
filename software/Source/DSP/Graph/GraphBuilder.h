#pragma once

#include "NodeGraph.h"

// Thin fluent helper for constructing a NodeGraph in C++. Stands in for the
// future visual editor - every Presets/*.cpp file uses this to build a graph
// equivalent to what a user would eventually wire up by hand.
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

    // Wires a specific output port of `src` (for multi-output nodes like
    // source.spinClassification) into `dst`'s input port `dstPort`.
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
