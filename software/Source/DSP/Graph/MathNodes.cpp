#include "../MathHelpers.h"
#include "NodeTypeRegistry.h"
#include <cmath>

// Math/dynamics node eval functions. Stateless nodes ignore the NodeState*
// argument; stateful ones share one generic scratch struct (MathNodeState)
// rather than one bespoke struct per node type - see the plan doc for why
// each of these exists (each maps to a concrete pattern found across the 11
// existing mapping strategies).

namespace Graph {
namespace {

struct MathNodeState : NodeState {
    float a = 0.0f;
    float b = 0.0f;
    bool flag = false;
    double sampleRate = 44100.0;

    void prepare(double sr) override { sampleRate = sr; reset(); }
    void reset() override { a = 0.0f; b = 0.0f; flag = false; }
};

std::unique_ptr<NodeState> makeMathState() { return std::make_unique<MathNodeState>(); }

void addStateless(NodeTypeRegistry& registry, const char* id, const char* name, const char* subcategory, int numInputs,
                   std::vector<float> defaultParams, NodeTypeInfo::MathEvalFn eval,
                   std::vector<juce::String> inputNames = {}, std::vector<juce::String> paramNames = {},
                   const char* description = "") {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Math;
    info.subcategory = subcategory;
    info.description = description;
    info.numInputs = numInputs;
    info.inputNames = std::move(inputNames);
    info.defaultParams = std::move(defaultParams);
    info.paramNames = std::move(paramNames);
    info.mathEval = eval;
    registry.registerType(std::move(info));
}

void addStateful(NodeTypeRegistry& registry, const char* id, const char* name, const char* subcategory, int numInputs,
                  std::vector<float> defaultParams, NodeTypeInfo::MathEvalFn eval,
                  std::vector<juce::String> inputNames = {}, std::vector<juce::String> paramNames = {},
                  const char* description = "") {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Math;
    info.subcategory = subcategory;
    info.description = description;
    info.numInputs = numInputs;
    info.inputNames = std::move(inputNames);
    info.defaultParams = std::move(defaultParams);
    info.paramNames = std::move(paramNames);
    info.isStateful = true;
    info.makeState = makeMathState;
    info.mathEval = eval;
    registry.registerType(std::move(info));
}

} // namespace

void registerMathNodes(NodeTypeRegistry& registry) {
    addStateless(registry, "math.constant", "Constant", "Arithmetic", 0, { 0.0f },
        [](const float*, int, const std::vector<float>& p, NodeState*, float* out) { out[0] = p.empty() ? 0.0f : p[0]; },
        {}, { "value" }, "Outputs its 'value' parameter every block, unchanged. Use to feed a fixed number into another node's input.");

    addStateless(registry, "math.add", "Add", "Arithmetic", 2, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0] + in[1]; },
        { "a", "b" }, {}, "out = a + b.");

    addStateless(registry, "math.subtract", "Subtract", "Arithmetic", 2, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0] - in[1]; },
        { "a", "b" }, {}, "out = a - b.");

    addStateless(registry, "math.multiply", "Multiply", "Arithmetic", 2, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0] * in[1]; },
        { "a", "b" }, {}, "out = a * b.");

    addStateless(registry, "math.divide", "Divide", "Arithmetic", 2, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) {
            out[0] = std::abs(in[1]) > 1e-6f ? in[0] / in[1] : 0.0f;
        }, { "a", "b" }, {}, "out = a / b. Outputs 0.0 if b is (near) zero, to avoid a divide-by-zero blowup.");

    addStateless(registry, "math.abs", "Absolute Value", "Arithmetic", 1, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = std::abs(in[0]); },
        { "value" }, {}, "out = |value|.");

    addStateless(registry, "math.max", "Max", "Arithmetic", 2, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = juce::jmax(in[0], in[1]); },
        { "a", "b" }, {}, "out = the larger of a and b.");

    addStateless(registry, "math.min", "Min", "Arithmetic", 2, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = juce::jmin(in[0], in[1]); },
        { "a", "b" }, {}, "out = the smaller of a and b.");

    addStateless(registry, "math.equals", "Equals", "Arithmetic", 2, { 0.5f },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = std::abs(in[0] - in[1]) <= p[0] ? 1.0f : 0.0f;
        }, { "a", "b" }, { "epsilon" }, "out = 1.0 if |a - b| <= epsilon, else 0.0.");

    addStateless(registry, "math.mapRange", "Map Range", "Shaping", 1, { 0.0f, 1.0f, 0.0f, 1.0f },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = juce::jmap(in[0], p[0], p[1], p[2], p[3]);
        }, { "value" }, { "inMin", "inMax", "outMin", "outMax" },
        "Linearly remaps value from [inMin, inMax] to [outMin, outMax]. Not clamped - it extrapolates outside "
        "the input range, so add a Clamp node after it if you need hard limits.");

    addStateless(registry, "math.clamp", "Clamp", "Shaping", 1, { 0.0f, 1.0f },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = juce::jlimit(juce::jmin(p[0], p[1]), juce::jmax(p[0], p[1]), in[0]);
        }, { "value" }, { "min", "max" }, "out = value limited to [min, max] (order-independent - works even if min > max).");

    addStateless(registry, "math.threshold", "Threshold", "Shaping", 1, { 0.0f },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) { out[0] = in[0] > p[0] ? 1.0f : 0.0f; },
        { "value" }, { "threshold" }, "out = 1.0 if value > threshold, else 0.0.");

    addStateless(registry, "math.crossfade", "Crossfade", "Shaping", 3, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) {
            const float mix = juce::jlimit(0.0f, 1.0f, in[2]);
            out[0] = in[0] + (in[1] - in[0]) * mix;
        }, { "a", "b", "mix" }, {},
        "out = a when mix=0.0, b when mix=1.0, linearly interpolated in between. mix is clamped to [0, 1].");

    addStateless(registry, "math.quantizeSteps", "Quantize Steps", "Shaping", 1, { 1.0f },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            const float step = p[0];
            out[0] = std::abs(step) > 1e-6f ? std::round(in[0] / step) * step : in[0];
        }, { "value" }, { "step" }, "Rounds value to the nearest multiple of 'step'. step=0 passes value through unchanged.");

    // Identity passthrough - used to "tap" a single output port off a
    // multi-output node (e.g. source.spinClassification) into its own node
    // id, so it can be wired anywhere a single-output value is expected.
    addStateless(registry, "math.passthrough", "Passthrough", "Shaping", 1, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0]; },
        { "value" }, {},
        "out = value, unchanged. Useful for tapping one output port of a multi-output node (e.g. Spin "
        "Classification) so it can be wired anywhere a single-output value is expected.");

    addStateless(registry, "math.sine", "Sine", "Shaping", 1, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = std::sin(in[0]); },
        { "phase" }, {}, "out = sin(phase). phase is in radians.");

    addStateless(registry, "math.atan2", "Atan2 (y, x)", "Shaping", 2, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = std::atan2(in[0], in[1]); },
        { "y", "x" }, {}, "out = atan2(y, x), in radians, range -pi..pi.");

    addStateless(registry, "math.semitonesToHz", "Semitones to Hz", "Shaping", 1, { 110.0f },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = MathHelpers::convertSemitonesToHertz(in[0], p[0]);
        }, { "semitones" }, { "rootHz" },
        "Converts a semitone offset to a frequency in Hz relative to rootHz (out = rootHz * 2^(semitones/12)).");

    addStateless(registry, "math.lookupTable", "Lookup Table", "Lookup Tables", 1, {},
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            if (p.empty()) { out[0] = 0.0f; return; }
            const int idx = juce::jlimit(0, static_cast<int>(p.size()) - 1, static_cast<int>(std::lround(in[0])));
            out[0] = p[static_cast<size_t>(idx)];
        }, { "index" }, {},
        "Reads the params table at round(index), clamped to the table's bounds, and outputs that value. "
        "Params are freeform - edit each slot's value directly.");

    addStateless(registry, "math.lut3", "Lookup Table (3 selectors)", "Lookup Tables", 3, std::vector<float>(8, 0.0f),
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            if (p.empty()) { out[0] = 0.0f; return; }
            auto bit = [](float v) { return v >= 0.5f ? 1 : 0; };
            const int idx = juce::jlimit(0, static_cast<int>(p.size()) - 1, bit(in[0]) * 4 + bit(in[1]) * 2 + bit(in[2]));
            out[0] = p[static_cast<size_t>(idx)];
        }, { "i0", "i1", "i2" }, {},
        "3-bit lookup table: treats i0/i1/i2 as booleans (>=0.5 = 1) and outputs params[i0*4 + i1*2 + i2] "
        "from the 8-value table.");

    // Input 1 (coeff) is optional: leave it unconnected (its default falls
    // back to params[0]) for a fixed rate, or wire a computed value into it
    // for a per-block-varying rate (e.g. Azimut's movement-onset-boosted
    // pitch morph speed).
    addStateful(registry, "math.onePoleSmoother", "One-Pole Smoother", "Dynamics", 2, { 0.1f },
        [](const float* in, int, const std::vector<float>&, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            s->a = MathHelpers::applyOnePoleFilter(s->a, in[0], in[1]);
            out[0] = s->a;
        }, { "target", "rate" }, { "rate" },
        "Exponentially smooths 'target' toward its new value each block at 'rate' (0..1, higher = faster). "
        "The 'rate' input overrides the rate param when connected; leave it unconnected to use a fixed rate.");

    addStateful(registry, "math.lfoSine", "LFO (Sine)", "Dynamics", 0, { 5.0f },
        [](const float*, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            constexpr float kTwoPi = 6.28318530717958f;
            s->a += p[0] * kTwoPi / static_cast<float>(s->sampleRate > 0.0 ? s->sampleRate : 44100.0);
            if (s->a >= kTwoPi) s->a -= kTwoPi;
            out[0] = std::sin(s->a);
        }, {}, { "rateHz" },
        "Free-running sine oscillator at 'rateHz' Hz, output range -1.0..1.0. No input - its phase just keeps "
        "advancing every block.");

    addStateful(registry, "math.leakyIntegrator", "Leaky Integrator", "Dynamics", 1, { 0.99f },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            s->a = juce::jlimit(0.0f, 1.0f, s->a * p[0] + in[0]);
            out[0] = s->a;
        }, { "add" }, { "decay" },
        "Accumulates 'add' each block, decaying the running total by 'decay' (0..1) first. Output is clamped to [0, 1].");

    addStateful(registry, "math.retriggerEnvelope", "Retrigger Envelope", "Dynamics", 1, { 0.9f },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            const bool above = in[0] > 0.5f;
            if (above && !s->flag) s->a = 1.0f;
            else s->a *= p[0];
            s->flag = above;
            out[0] = s->a;
        }, { "gate" }, { "decay" },
        "One-shot decay envelope retriggered by 'gate' crossing above 0.5: jumps to 1.0 on the rising edge, "
        "then decays by multiplying by 'decay' (0..1) every subsequent block.");

    addStateful(registry, "math.hysteresisStep", "Hysteresis Step", "Dynamics", 1, { 0.5f },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            if (!s->flag || std::abs(in[0] - s->a) > p[0]) {
                s->a = std::round(in[0]);
                s->flag = true;
            }
            out[0] = s->a;
        }, { "candidate" }, { "width" },
        "Snaps 'candidate' to the nearest whole number, but only updates its held output once |candidate - held| "
        "exceeds 'width' - reduces chatter around integer boundaries.");

    addStateful(registry, "math.derivative", "Derivative", "Dynamics", 1, {},
        [](const float* in, int, const std::vector<float>&, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            out[0] = s->flag ? (in[0] - s->a) : 0.0f;
            s->a = in[0];
            s->flag = true;
        }, { "value" }, {},
        "out = change in 'value' since the previous block (value[n] - value[n-1]). 0.0 on the very first block.");

    addStateful(registry, "math.latchedSmoother", "Latched Smoother", "Dynamics", 2, { 0.1f },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            if (in[1] > 0.5f) s->a = MathHelpers::applyOnePoleFilter(s->a, in[0], p[0]);
            out[0] = s->a;
        }, { "target", "gate" }, { "rate" },
        "While 'gate' > 0.5, smooths 'target' toward its new value at 'rate' (0..1); holds its last output "
        "whenever gate is <= 0.5.");
}

} // namespace Graph
