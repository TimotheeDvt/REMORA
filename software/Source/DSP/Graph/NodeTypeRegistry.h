#pragma once

#include "GraphTypes.h"
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

struct MappingOutput;

namespace Graph {

enum class NodeCategory { Source, Math, Sink, Display };

enum class DisplayKind { None, Number, Meter, Scope };

struct NodeTypeInfo {
    juce::String id;
    juce::String displayName;
    NodeCategory category = NodeCategory::Math;
    juce::String subcategory;

    juce::String description;

    int numInputs = 0;
    std::vector<juce::String> inputNames;
    // Value an input port falls back to when left unconnected. Empty = every port defaults to 0
    // (the InputSlot default); otherwise must be sized to numInputs. Lets a mega-sink's unwired
    // ports fall back to a sensible neutral value instead of 0 (e.g. an unwired LPF cutoff should
    // mean "wide open", not "fully closed").
    std::vector<float> inputDefaults;
    std::vector<float> defaultParams;   // also defines numParams via .size()
    std::vector<juce::String> paramNames;

    int numOutputs = 1;
    std::vector<juce::String> outputNames; // only meaningful when numOutputs > 1
    bool isStateful = false;

    // Node box width in the graph editor. 0 = use the standard width; set this for nodes whose
    // port names would otherwise get cropped (e.g. a mega-sink with many long-named ports).
    float defaultWidth = 0.0f;

    // Sink-only: range for the MonitorParam(s) GraphMappingStrategy auto-registers, one pair per
    // input port (a single-input sink has one entry; a mega-sink has one entry per parameter).
    std::vector<float> monitorRangeMin = { 0.0f };
    std::vector<float> monitorRangeMax = { 1.0f };

    DisplayKind displayKind = DisplayKind::None;
    float displayDefaultWidth = 130.0f;
    float displayDefaultHeight = 80.0f;

    using SourceEvalFn = void (*)(const SourceFrame&, const std::vector<float>& params, NodeState* state, float* outputs);
    using MathEvalFn = void (*)(const float* inputs, int numInputs, const std::vector<float>& params, NodeState* state, float* outputs);
    using SinkWriteFn = void (*)(const float* inputs, const std::vector<float>& params, MappingOutput& out);

    SourceEvalFn sourceEval = nullptr;
    MathEvalFn mathEval = nullptr;
    SinkWriteFn sinkWrite = nullptr;

    std::function<std::unique_ptr<NodeState>()> makeState; // set only if isStateful
};

inline juce::String formatRangeNumber(float v) {
    return (std::abs(v - std::round(v)) < 0.001f)
        ? juce::String(static_cast<int>(std::round(v)))
        : juce::String(v, 2);
}

// Lazily-populated singleton catalog of every node type - see NodeMetadata.cpp.
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
