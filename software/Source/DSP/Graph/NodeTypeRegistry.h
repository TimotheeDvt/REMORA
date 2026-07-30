#pragma once

#include "GraphTypes.h"
#include <functional>
#include <unordered_map>
#include <vector>

struct MappingOutput;

namespace Graph {

enum class NodeCategory { Source, Math, Sink };

struct NodeTypeInfo {
    juce::String id;
    juce::String displayName;
    NodeCategory category = NodeCategory::Math;

    int numInputs = 0;
    std::vector<juce::String> inputNames;
    std::vector<float> defaultParams;   // also defines numParams via .size()

    int numOutputs = 1;
    std::vector<juce::String> outputNames; // only meaningful when numOutputs > 1
    bool isStateful = false;

    // Sink-only: range used for the MonitorParam auto-registered by
    // GraphMappingStrategy so the existing DSP window can show it.
    float monitorRangeMin = 0.0f;
    float monitorRangeMax = 1.0f;

    using SourceEvalFn = void (*)(const SourceFrame&, const std::vector<float>& params, NodeState* state, float* outputs);
    using MathEvalFn = void (*)(const float* inputs, int numInputs, const std::vector<float>& params, NodeState* state, float* outputs);
    using SinkWriteFn = void (*)(const float* inputs, const std::vector<float>& params, MappingOutput& out);

    SourceEvalFn sourceEval = nullptr;
    MathEvalFn mathEval = nullptr;
    SinkWriteFn sinkWrite = nullptr;

    std::function<std::unique_ptr<NodeState>()> makeState; // set only if isStateful
};

// Lazily-populated singleton catalog of every node type. Populated once, on
// first use, by registerBuiltinSourceNodes/MathNodes/SinkNodes (see
// SourceNodes.cpp/MathNodes.cpp/SinkNodes.cpp).
class NodeTypeRegistry {
public:
    static NodeTypeRegistry& instance();

    void registerType(NodeTypeInfo info);
    const NodeTypeInfo* find(const juce::String& typeId) const;
    const std::vector<NodeTypeInfo>& all() const { return types_; }

private:
    NodeTypeRegistry();

    std::vector<NodeTypeInfo> types_;
    std::unordered_map<std::string, size_t> indexById_;
};

} // namespace Graph
