#pragma once

#include "../GraphBuilder.h"

// Small conveniences shared by every Presets/*.cpp builder - each existing
// mapping's process() leaned heavily on "scale by a constant", "add a
// constant", "multiply two signals", etc., so these read the same way the
// original C++ did instead of forcing every preset to hand-build a
// math.constant + math.multiply pair inline each time.
namespace Graph::Presets {

inline NodeId constantNode(GraphBuilder& b, float value) {
    return b.add("math.constant", { value });
}

// out = src * factor (implemented as a mapRange 0..1 -> 0..factor, so it's a
// single node rather than a constant+multiply pair).
inline NodeId scale(GraphBuilder& b, NodeId src, float factor) {
    NodeId n = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, factor });
    b.wire(src, n);
    return n;
}

inline NodeId addConst(GraphBuilder& b, NodeId src, float value) {
    NodeId n = b.add("math.add");
    b.wire(src, n, 0);
    b.wire(constantNode(b, value), n, 1);
    return n;
}

inline NodeId subConst(GraphBuilder& b, NodeId src, float value) {
    // value - src (matches the many "X - semitones" patterns in the
    // original code, where `src` is the thing being subtracted).
    NodeId n = b.add("math.subtract");
    b.wire(constantNode(b, value), n, 0);
    b.wire(src, n, 1);
    return n;
}

inline NodeId clampNode(GraphBuilder& b, NodeId src, float lo, float hi) {
    NodeId n = b.add("math.clamp", { lo, hi });
    b.wire(src, n);
    return n;
}

// Reads a specific output port of a multi-output node (e.g.
// source.spinClassification) as its own single-output node id.
inline NodeId tapPort(GraphBuilder& b, NodeId src, int port) {
    NodeId n = b.add("math.passthrough");
    b.wire(src, port, n, 0);
    return n;
}

inline NodeId absNode(GraphBuilder& b, NodeId src) {
    NodeId n = b.add("math.abs");
    b.wire(src, n);
    return n;
}

inline NodeId mulNodes(GraphBuilder& b, NodeId a, NodeId c) {
    NodeId n = b.add("math.multiply");
    b.wire(a, n, 0);
    b.wire(c, n, 1);
    return n;
}

inline NodeId addNodes(GraphBuilder& b, NodeId a, NodeId c) {
    NodeId n = b.add("math.add");
    b.wire(a, n, 0);
    b.wire(c, n, 1);
    return n;
}

inline NodeId subNodes(GraphBuilder& b, NodeId a, NodeId c) {
    NodeId n = b.add("math.subtract");
    b.wire(a, n, 0);
    b.wire(c, n, 1);
    return n;
}

// math.onePoleSmoother at a fixed rate (its 2nd input, coeff, is left
// unconnected and defaults to `coeff`).
inline NodeId onePole(GraphBuilder& b, NodeId target, float coeff) {
    NodeId n = b.add("math.onePoleSmoother", { coeff });
    b.wire(target, n, 0);
    b.graph().setInputDefault(n, 1, coeff);
    return n;
}

// math.onePoleSmoother with a per-block-computed rate wired into its 2nd
// input (e.g. Azimut's movement-onset-boosted morph speed).
inline NodeId onePoleVariableRate(GraphBuilder& b, NodeId target, NodeId coeffSrc) {
    NodeId n = b.add("math.onePoleSmoother", { 0.1f });
    b.wire(target, n, 0);
    b.wire(coeffSrc, n, 1);
    return n;
}

inline NodeId threshold(GraphBuilder& b, NodeId src, float t) {
    NodeId n = b.add("math.threshold", { t });
    b.wire(src, n);
    return n;
}

// Shared by the Azimut family and Bozendo (the original C++ duplicated these
// exact formulas across mapping files too - see AzimutMapping.cpp and
// BozendoMapping.cpp's near-identical applyMasterGainToOutput/
// applyNoiseToOutput).

// motionGate = clamp(mapRange(gyroMag, floor*0.5, floor*2, 0, 1), 0, 1)
inline NodeId standardMotionGate(GraphBuilder& b, NodeId gyroMag, float gyroscopeFloor) {
    NodeId raw = b.add("math.mapRange", { gyroscopeFloor * 0.5f, gyroscopeFloor * 2.0f, 0.0f, 1.0f });
    b.wire(gyroMag, raw);
    return clampNode(b, raw, 0.0f, 1.0f);
}

// masterGain = clamp(motionGate * (0.05 + labanWeight*0.70) + thrustPeak*0.6, 0, 1)
inline NodeId standardMasterGain(GraphBuilder& b, NodeId motionGate, NodeId labanWeight, NodeId thrustPeak) {
    NodeId gated = mulNodes(b, motionGate, addConst(b, scale(b, labanWeight, 0.70f), 0.05f));
    return clampNode(b, addNodes(b, gated, scale(b, thrustPeak, 0.6f)), 0.0f, 1.0f);
}

// noiseEnvelope: leaky integrator charged by max(0, suddenness-0.3)*1.5 plus
// thrustPeak*0.5, decaying at `decay` per block.
inline NodeId standardNoiseEnvelope(GraphBuilder& b, NodeId suddenness, NodeId thrustPeak, float decay) {
    NodeId gate = threshold(b, suddenness, 0.3f);
    NodeId excess = subNodes(b, suddenness, constantNode(b, 0.3f));
    NodeId term1 = scale(b, mulNodes(b, excess, gate), 1.5f);
    NodeId term2 = scale(b, thrustPeak, 0.5f);
    NodeId env = b.add("math.leakyIntegrator", { decay });
    b.wire(addNodes(b, term1, term2), env);
    return env;
}

inline NodeId crossfadeNodes(GraphBuilder& b, NodeId a, NodeId bVal, NodeId mix) {
    NodeId n = b.add("math.crossfade");
    b.wire(a, n, 0);
    b.wire(bVal, n, 1);
    b.wire(mix, n, 2);
    return n;
}

inline void toSink(GraphBuilder& b, NodeId src, const char* sinkTypeId, std::vector<float> params = {}) {
    b.wire(src, b.add(sinkTypeId, std::move(params)));
}

} // namespace Graph::Presets
