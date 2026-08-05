#pragma once

#include "../GraphBuilder.h"

// Mirrors the "scale by a constant"/"add a constant" patterns the original hand-written mappings used.
namespace Graph::Presets {

inline NodeId constantNode(GraphBuilder& b, float value) {
    return b.add("math.constant", { value });
}

// out = src * factor, as a single mapRange node rather than a constant+multiply pair.
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

// value - src, matching the many "X - semitones" patterns in the original code.
inline NodeId subConst(GraphBuilder& b, NodeId src, float value) {
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

// Reads one output port of a multi-output node (e.g. spinClassification) as its own single-output node.
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

// Fixed rate - the 2nd input (coeff) is left unconnected and defaults to `coeff`.
inline NodeId onePole(GraphBuilder& b, NodeId target, float coeff) {
    NodeId n = b.add("math.onePoleSmoother", { coeff });
    b.wire(target, n, 0);
    b.graph().setInputDefault(n, 1, coeff);
    return n;
}

// Per-block-computed rate wired into the 2nd input (e.g. Azimut's movement-onset-boosted morph speed).
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

// Shared by the Azimut and Martial families, which duplicated these exact formulas in the original C++.

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

// noiseEnvelope = leaky integrator charged by max(0, suddenness-0.3)*1.5 + thrustPeak*0.5, decaying at `decay`/block.
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
