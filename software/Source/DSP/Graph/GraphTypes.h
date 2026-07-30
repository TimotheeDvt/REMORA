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

// Fixed upper bound on a single node's output count. Only the Spin
// Classification source node (isVertical, spinDirection, continuousSpinCount,
// isFacingNorth) needs more than one output in the v1 catalog.
constexpr int kMaxNodeOutputs = 4;
// Upper bound on a single node's input port count (lut3/crossfade use 3).
constexpr int kMaxNodeInputs = 4;

// One input port: either wired to another node's output port, or - if
// unconnected - falls back to defaultValue.
struct InputSlot {
    NodeId sourceNode = kInvalidNodeId;
    int sourceOutputPort = 0;
    float defaultValue = 0.0f;
};

// Base class for per-instance mutable state (smoothers, phase accumulators,
// latches...). Stateless node types never allocate one.
struct NodeState {
    virtual ~NodeState() = default;
    virtual void prepare(double sampleRate) { juce::ignoreUnused(sampleRate); reset(); }
    virtual void reset() {}
};

// Snapshot passed into every node's eval function for one block. `analyzer`
// is a live, mutable reference (not just a copy of derived values) because
// the Spin Classification node must call back into it directly - see
// SourceNodes.cpp.
struct SourceFrame {
    const StaffSoundParams& raw;
    const StaffMotionAnalyzer::DerivedMotionFrame& derived;
    StaffMotionAnalyzer& analyzer;
};

} // namespace Graph
