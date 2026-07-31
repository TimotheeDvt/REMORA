#pragma once

#include "../StaffMotionAnalyzer.h"
#include <JuceHeader.h>
#include <array>
#include <cstdint>
#include <memory>

struct StaffSoundParams;
struct MappingOutput;

namespace Graph {

using NodeId = int32_t;
constexpr NodeId kInvalidNodeId = -1;

// Only Spin Classification needs more than one output in the v1 catalog
constexpr int kMaxNodeOutputs = 4;
// Upper bound on a single node's input port count (lut3/crossfade use 3)
constexpr int kMaxNodeInputs = 4;

// Unconnected falls back to defaultValue.
struct InputSlot {
    NodeId sourceNode = kInvalidNodeId;
    int sourceOutputPort = 0;
    float defaultValue = 0.0f;
};

// Stateless node types never allocate one.
struct NodeState {
    virtual ~NodeState() = default;
    virtual void prepare(double sampleRate) { juce::ignoreUnused(sampleRate); reset(); }
    virtual void reset() {}
};

// `analyzer` is a live, mutable reference since Spin Classification must call back into it directly.
struct SourceFrame {
    const StaffSoundParams& raw;
    const StaffMotionAnalyzer::DerivedMotionFrame& derived;
    StaffMotionAnalyzer& analyzer;
};

} // namespace Graph
