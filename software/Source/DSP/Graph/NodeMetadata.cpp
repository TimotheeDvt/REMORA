#include "../BoStaffSynth.h"
#include "../IMappingStrategy.h"
#include "../MathHelpers.h"
#include "NodeMetadata.h"
#include <cmath>

// Every node's full definition lives here; add a node by adding one call to buildAllNodes() below.

namespace Graph {
namespace {

// Stateful math nodes share this one scratch struct instead of a bespoke one each.
struct MathNodeState : NodeState {
    float a = 0.0f;
    float b = 0.0f;
    bool flag = false;
    double sampleRate = 44100.0;

    void prepare(double sr) override { sampleRate = sr; reset(); }
    void reset() override { a = 0.0f; b = 0.0f; flag = false; }
};

std::unique_ptr<NodeState> makeMathState() { return std::make_unique<MathNodeState>(); }

template <int N>
int arrayIndex(const std::vector<float>& params) {
    const float raw = params.empty() ? 0.0f : params[0];
    return juce::jlimit(0, N - 1, static_cast<int>(std::lround(raw)));
}

NodeTypeInfo math(const char* id, const char* name, const char* subcategory, const char* description,
                   std::vector<juce::String> inputNames, std::vector<float> defaultParams,
                   std::vector<juce::String> paramNames, NodeTypeInfo::MathEvalFn eval, bool stateful = false) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Math;
    info.subcategory = subcategory;
    info.description = description;
    info.inputNames = std::move(inputNames);
    info.numInputs = static_cast<int>(info.inputNames.size());
    info.defaultParams = std::move(defaultParams);
    info.paramNames = std::move(paramNames);
    info.mathEval = eval;
    if (stateful) {
        info.isStateful = true;
        info.makeState = makeMathState;
    }
    return info;
}

NodeTypeInfo source(const char* id, const char* name, const char* subcategory, const char* description,
                     NodeTypeInfo::SourceEvalFn eval, std::vector<float> defaultParams = {},
                     std::vector<juce::String> paramNames = {}, std::vector<juce::String> outputNames = {},
                     bool stateful = false) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Source;
    info.subcategory = subcategory;
    info.description = description;
    info.defaultParams = std::move(defaultParams);
    info.paramNames = std::move(paramNames);
    info.outputNames = std::move(outputNames);
    info.numOutputs = info.outputNames.empty() ? 1 : static_cast<int>(info.outputNames.size());
    info.sourceEval = eval;
    if (stateful) {
        info.isStateful = true;
        info.makeState = makeMathState;
    }
    return info;
}

NodeTypeInfo sink(const char* id, const char* name, const char* subcategory, const char* description,
                   float monitorRangeMin, float monitorRangeMax, NodeTypeInfo::SinkWriteFn write,
                   std::vector<float> defaultParams = {}, std::vector<juce::String> paramNames = {}) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Sink;
    info.subcategory = subcategory;
    info.description = description;
    info.numInputs = 1;
    info.inputNames = { "value" };
    info.defaultParams = std::move(defaultParams);
    info.paramNames = std::move(paramNames);
    info.monitorRangeMin = monitorRangeMin;
    info.monitorRangeMax = monitorRangeMax;
    info.sinkWrite = write;
    return info;
}

#define REMORA_RAW_EVAL(expr) \
    [](const SourceFrame& sf, const std::vector<float>&, NodeState*, float* out) { out[0] = (expr); }

std::vector<NodeTypeInfo> buildAllNodes() {
    std::vector<NodeTypeInfo> nodes;

    // Raw sensor
    nodes.push_back(source("source.pitch", "Pitch", "Raw Sensor",
        "Calibrated pitch angle of the staff (radians).\nRange: [-pi/2 ; pi/2]",
        REMORA_RAW_EVAL(sf.raw.pitch)));
    nodes.push_back(source("source.roll", "Roll", "Raw Sensor",
        "Calibrated roll angle of the staff (radians).\nRange: [-pi ; pi]",
        REMORA_RAW_EVAL(sf.raw.roll)));
    nodes.push_back(source("source.yaw", "Yaw", "Raw Sensor",
        "Calibrated yaw (heading) angle of the staff (radians).\nRange: [-pi ; pi]",
        REMORA_RAW_EVAL(sf.raw.yaw)));
    nodes.push_back(source("source.gyroX", "Gyro X", "Raw Sensor",
        "Raw angular velocity, X axis (deg/s), unfiltered.\nRange: unbounded",
        REMORA_RAW_EVAL(sf.raw.gx)));
    nodes.push_back(source("source.gyroY", "Gyro Y", "Raw Sensor",
        "Raw angular velocity, Y axis (deg/s), unfiltered.\nRange: unbounded",
        REMORA_RAW_EVAL(sf.raw.gy)));
    nodes.push_back(source("source.gyroZ", "Gyro Z", "Raw Sensor",
        "Raw angular velocity, Z axis (deg/s), unfiltered.\nRange: unbounded",
        REMORA_RAW_EVAL(sf.raw.gz)));
    nodes.push_back(source("source.accelX", "Accel X", "Raw Sensor",
        "Raw linear acceleration, X axis (g), includes gravity.\nRange: unbounded",
        REMORA_RAW_EVAL(sf.raw.ax)));
    nodes.push_back(source("source.accelY", "Accel Y", "Raw Sensor",
        "Raw linear acceleration, Y axis (g), includes gravity.\nRange: unbounded",
        REMORA_RAW_EVAL(sf.raw.ay)));
    nodes.push_back(source("source.accelZ", "Accel Z", "Raw Sensor",
        "Raw linear acceleration, Z axis (g), includes gravity.\nRange: unbounded",
        REMORA_RAW_EVAL(sf.raw.az)));
    nodes.push_back(source("source.magX", "Mag X", "Raw Sensor",
        "Raw magnetometer reading, X axis (uT).\nRange: unbounded", REMORA_RAW_EVAL(sf.raw.mx)));
    nodes.push_back(source("source.magY", "Mag Y", "Raw Sensor",
        "Raw magnetometer reading, Y axis (uT).\nRange: unbounded", REMORA_RAW_EVAL(sf.raw.my)));
    nodes.push_back(source("source.magZ", "Mag Z", "Raw Sensor",
        "Raw magnetometer reading, Z axis (uT).\nRange: unbounded", REMORA_RAW_EVAL(sf.raw.mz)));
    nodes.push_back(source("source.quatW", "Quat W", "Raw Sensor",
        "Calibrated orientation quaternion, W component.\nRange: [-1 ; 1]",
        REMORA_RAW_EVAL(sf.raw.qw)));
    nodes.push_back(source("source.quatX", "Quat X", "Raw Sensor",
        "Calibrated orientation quaternion, X component.\nRange: [-1 ; 1]",
        REMORA_RAW_EVAL(sf.raw.qx)));
    nodes.push_back(source("source.quatY", "Quat Y", "Raw Sensor",
        "Calibrated orientation quaternion, Y component.\nRange: [-1 ; 1]",
        REMORA_RAW_EVAL(sf.raw.qy)));
    nodes.push_back(source("source.quatZ", "Quat Z", "Raw Sensor",
        "Calibrated orientation quaternion, Z component.\nRange: [-1 ; 1]",
        REMORA_RAW_EVAL(sf.raw.qz)));
    nodes.push_back(source("source.isReceivingValidData", "Valid Data", "Raw Sensor",
        "1 while the staff sensor is sending valid data; 0 if stale/lost.\nRange: 0 or 1",
        REMORA_RAW_EVAL(sf.raw.isReceivingValidData ? 1.0f : 0.0f)));

    // Bypasses the shared analyzer - LeadDrone/SpinFilter/BowedChord want instantaneous raw magnitude, not smoothed.
    nodes.push_back(source("source.gyroMagnitudeRaw", "Gyro Magnitude (Raw)", "Raw Sensor",
        "Instantaneous raw gyro magnitude (deg/s), unsmoothed.\nRange: [0 ; unbounded)",
        REMORA_RAW_EVAL(std::sqrt(sf.raw.gx * sf.raw.gx + sf.raw.gy * sf.raw.gy + sf.raw.gz * sf.raw.gz))));
    nodes.push_back(source("source.accelMagnitudeRaw", "Accel Magnitude (Raw)", "Raw Sensor",
        "Instantaneous raw accel magnitude (g), includes gravity, unsmoothed.\nRange: [0 ; unbounded)",
        REMORA_RAW_EVAL(std::sqrt(sf.raw.ax * sf.raw.ax + sf.raw.ay * sf.raw.ay + sf.raw.az * sf.raw.az))));

    // Derived motion
    nodes.push_back(source("source.gyroMagnitude", "Gyro Magnitude", "Derived Motion",
        "Smoothed gyroscope magnitude (deg/s); feeds the isMoving gate (30 deg/s threshold).\n"
        "Range: [0 ; unbounded), typically 0-750+",
        REMORA_RAW_EVAL(sf.derived.smoothedGyroscopeMagnitude)));
    nodes.push_back(source("source.labanWeight", "Laban Weight", "Derived Motion",
        "Laban 'Weight' effort: how forceful the motion is.\nRange: [0 ; 1]",
        REMORA_RAW_EVAL(sf.derived.labanWeight)));
    nodes.push_back(source("source.labanTimeSuddenness", "Laban Time (Suddenness)", "Derived Motion",
        "Laban 'Time' effort: sustained (0) vs sudden (1).\nRange: [0 ; 1]",
        REMORA_RAW_EVAL(sf.derived.labanTimeSuddenness)));
    nodes.push_back(source("source.labanSpaceFocus", "Laban Space Focus", "Derived Motion",
        "Laban 'Space' effort: flexible/indirect (0) vs direct/focused (1) pathing.\nRange: [0 ; 1]",
        REMORA_RAW_EVAL(sf.derived.labanSpaceFocus)));
    nodes.push_back(source("source.labanFlowBound", "Laban Flow Bound", "Derived Motion",
        "Laban 'Flow' effort, bound side: free (0) to fully bound/controlled (1).\nRange: [0 ; 1]",
        REMORA_RAW_EVAL(sf.derived.labanFlowBound)));
    nodes.push_back(source("source.labanFlowFree", "Laban Flow Free", "Derived Motion",
        "Laban 'Flow' effort, free side: bound (0) to fully free/fluid (1).\nRange: [0 ; 1]",
        REMORA_RAW_EVAL(sf.derived.labanFlowFree)));
    nodes.push_back(source("source.thrustPeakEnvelope", "Thrust Peak Envelope", "Derived Motion",
        "Spikes toward 1 on a sharp axial thrust/jab, decays back to 0 after.\nRange: [0 ; 1]",
        REMORA_RAW_EVAL(sf.derived.axialThrustPeakEnvelope)));
    nodes.push_back(source("source.rotationAxisX", "Rotation Axis X", "Derived Motion",
        "Smoothed world-frame rotation axis, X component.\nRange: [-1 ; 1]",
        REMORA_RAW_EVAL(sf.derived.rotationAxisX)));
    nodes.push_back(source("source.rotationAxisY", "Rotation Axis Y", "Derived Motion",
        "Smoothed world-frame rotation axis, Y component.\nRange: [-1 ; 1]",
        REMORA_RAW_EVAL(sf.derived.rotationAxisY)));
    nodes.push_back(source("source.deltaTime", "Delta Time (s)", "Derived Motion",
        "Time elapsed since the previous audio block.\nRange: (0 ; unbounded), typically well under 0.1",
        REMORA_RAW_EVAL(sf.derived.deltaTimeSeconds)));
    nodes.push_back(source("source.isMoving", "Is Moving", "Derived Motion",
        "1 if smoothed gyro magnitude is above the moving threshold (30 deg/s).\nRange: 0 or 1",
        REMORA_RAW_EVAL(sf.derived.isMoving ? 1.0f : 0.0f)));

    // Has side effects (calls back into the live analyzer) since its behavior needs a per-node convention param.
    nodes.push_back(source("source.spinClassification", "Spin Classification", "Derived Motion",
        "Classifies the current spin plane/direction; holds last value while still.\n"
        "vertical: 0 (horizontal) or 1 (vertical)\n"
        "spin: -1 or 1, direction; 'convention' param selects the sign rule (0=absolute axis, 1=reference azimuth)\n"
        "count: full rotations completed in the current spin, resets on classification change\n"
        "facing: 0 or 1, 1 if the staff tip faces the calibrated 'north'",
        [](const SourceFrame& sf, const std::vector<float>& params, NodeState*, float* out) {
            const auto& d = sf.derived;
            bool spinChanged = false;

            if (d.isMoving) {
                const bool useReferenceAzimuth = !params.empty() && params[0] >= 0.5f;
                spinChanged = useReferenceAzimuth
                    ? sf.analyzer.updateSpinClassificationByReferenceAzimuth(d.rotationAxisX, d.rotationAxisY, d.rotationAxisZ)
                    : sf.analyzer.updateSpinClassificationByAbsoluteComponent(d.rotationAxisX, d.rotationAxisY, d.rotationAxisZ);
                sf.analyzer.updateFacingClassification(d.tipX, d.tipY);
                sf.analyzer.accumulateContinuousSpins(spinChanged, d.gyroscopeMagnitude);
            }

            out[0] = sf.analyzer.isRotationAxisVertical() ? 1.0f : 0.0f;
            out[1] = sf.analyzer.rotationSpinDirection();
            out[2] = static_cast<float>(sf.analyzer.continuousSpinCount());
            out[3] = sf.analyzer.isFacingNorth() ? 1.0f : 0.0f;
        },
        { 0.0f }, { "convention" }, { "vertical", "spin", "count", "facing" }));

    // Generator - takes no graph input, so it behaves like a source rather than a math operation.
    nodes.push_back(source("math.constant", "Constant", "Generator",
        "Outputs its 'value' param every block, unchanged.\nRange: set by 'value' param",
        [](const SourceFrame&, const std::vector<float>& p, NodeState*, float* out) { out[0] = p.empty() ? 0.0f : p[0]; },
        { 0.0f }, { "value" }));

    nodes.push_back(math("math.add", "Add", "Arithmetic",
        "Input range: any\nOutput range: any\nFormula: f(a, b) = a + b", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0] + in[1]; }));
    nodes.push_back(math("math.subtract", "Subtract", "Arithmetic",
        "Input range: any\nOutput range: any\nFormula: f(a, b) = a - b", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0] - in[1]; }));
    nodes.push_back(math("math.multiply", "Multiply", "Arithmetic",
        "Input range: any\nOutput range: any\nFormula: f(a, b) = a * b", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0] * in[1]; }));
    nodes.push_back(math("math.divide", "Divide", "Arithmetic",
        "Input range: any\nOutput range: any (0 if |b| < 1e-6)\nFormula: f(a, b) = a / b", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) {
            out[0] = std::abs(in[1]) > 1e-6f ? in[0] / in[1] : 0.0f;
        }));
    nodes.push_back(math("math.floorDiv", "Floor Divide", "Arithmetic",
        "Input range: any (b != 0)\nOutput range: integer-valued (0 if |b| < 1e-6)\nFormula: f(a, b) = floor(a / b)",
        { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) {
            out[0] = std::abs(in[1]) > 1e-6f ? std::floor(in[0] / in[1]) : 0.0f;
        }));
    nodes.push_back(math("math.mod", "Modulo", "Arithmetic",
        "Input range: any (b != 0)\nOutput range: [0 ; |b|) (0 if |b| < 1e-6)\n"
        "Formula: f(a, b) = a - floor(a / b) * b - always non-negative, unlike C's fmod",
        { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) {
            out[0] = std::abs(in[1]) > 1e-6f ? in[0] - std::floor(in[0] / in[1]) * in[1] : 0.0f;
        }));
    nodes.push_back(math("math.abs", "Absolute Value", "Arithmetic",
        "Input range: any\nOutput range: [0 ; unbounded)\nFormula: f(value) = |value|", { "value" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = std::abs(in[0]); }));
    nodes.push_back(math("math.max", "Max", "Arithmetic",
        "Input range: any\nOutput range: any\nFormula: f(a, b) = max(a, b)", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = juce::jmax(in[0], in[1]); }));
    nodes.push_back(math("math.min", "Min", "Arithmetic",
        "Input range: any\nOutput range: any\nFormula: f(a, b) = min(a, b)", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = juce::jmin(in[0], in[1]); }));
    nodes.push_back(math("math.equals", "Equals", "Arithmetic",
        "Input range: any\nOutput range: {0, 1}\nFormula: f(a, b) = |a - b| <= epsilon ? 1 : 0",
        { "a", "b" }, { 0.5f }, { "epsilon" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = std::abs(in[0] - in[1]) <= p[0] ? 1.0f : 0.0f;
        }));

    nodes.push_back(math("math.mapRange", "Map Range", "Shaping",
        "Input range: any (nominally [inMin, inMax])\nOutput range: unclamped, extrapolates outside [inMin, inMax]\n"
        "Formula: f(value) = map(value, inMin, inMax -> outMin, outMax)",
        { "value" }, { 0.0f, 1.0f, 0.0f, 1.0f }, { "inMin", "inMax", "outMin", "outMax" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = juce::jmap(in[0], p[0], p[1], p[2], p[3]);
        }));
    nodes.push_back(math("math.mapRangeLog", "Map Range (Log)", "Shaping",
        "Input range: any (nominally [inMin, inMax])\nOutput range: unclamped, extrapolates outside [inMin, inMax]\n"
        "Formula: f(value) = outMin * (outMax/outMin)^t, t = (value-inMin)/(inMax-inMin)\n"
        "Logarithmic (exponential) mapping: equal steps in the input produce equal ratios in the output, "
        "matching how pitch/frequency is perceived (unlike Map Range's equal steps -> equal differences).\n"
        "outMin and outMax must both be > 0 (falls back to a linear map otherwise).",
        { "value" }, { 0.0f, 1.0f, 20.0f, 2000.0f }, { "inMin", "inMax", "outMin", "outMax" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            const float inMin = p[0], inMax = p[1], outMin = p[2], outMax = p[3];
            const float denom = inMax - inMin;
            const float t = std::abs(denom) > 1e-6f ? (in[0] - inMin) / denom : 0.0f;
            out[0] = (outMin > 1e-6f && outMax > 1e-6f) ? outMin * std::pow(outMax / outMin, t)
                                                          : juce::jmap(t, 0.0f, 1.0f, outMin, outMax);
        }));
    nodes.push_back(math("math.clamp", "Clamp", "Shaping",
        "Input range: any\nOutput range: [min ; max]\nFormula: f(value) = clamp(value, min, max)",
        { "value" }, { 0.0f, 1.0f }, { "min", "max" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = juce::jlimit(juce::jmin(p[0], p[1]), juce::jmax(p[0], p[1]), in[0]);
        }));
    nodes.push_back(math("math.threshold", "Threshold", "Shaping",
        "Input range: any\nOutput range: {0, 1}\nFormula: f(value) = value > threshold ? 1 : 0",
        { "value" }, { 0.0f }, { "threshold" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) { out[0] = in[0] > p[0] ? 1.0f : 0.0f; }));
    nodes.push_back(math("math.crossfade", "Crossfade", "Shaping",
        "Input range: any (mix clamped to [0, 1])\nOutput range: between a and b\n"
        "Formula: f(a, b, mix) = a + (b - a) * clamp(mix, 0, 1)",
        { "a", "b", "mix" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) {
            const float mix = juce::jlimit(0.0f, 1.0f, in[2]);
            out[0] = in[0] + (in[1] - in[0]) * mix;
        }));
    nodes.push_back(math("math.quantizeSteps", "Quantize Steps", "Shaping",
        "Input range: any\nOutput range: multiples of 'step'\nFormula: f(value) = round(value / step) * step (step=0: passthrough)",
        { "value" }, { 1.0f }, { "step" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            const float step = p[0];
            out[0] = std::abs(step) > 1e-6f ? std::round(in[0] / step) * step : in[0];
        }));
    nodes.push_back(math("math.passthrough", "Passthrough", "Shaping",
        "Input range: any\nOutput range: same as input\nFormula: f(value) = value. Useful for tapping one port "
        "of a multi-output node (e.g. Spin Classification).",
        { "value" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0]; }));
    nodes.push_back(math("math.sine", "Sine", "Shaping",
        "Input range: radians\nOutput range: [-1 ; 1]\nFormula: f(phase) = sin(phase)", { "phase" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = std::sin(in[0]); }));
    nodes.push_back(math("math.atan2", "Atan2 (y, x)", "Shaping",
        "Input range: any\nOutput range: [-pi ; pi]\nFormula: f(y, x) = atan2(y, x)",
        { "y", "x" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = std::atan2(in[0], in[1]); }));
    nodes.push_back(math("math.semitonesToHz", "Semitones to Hz", "Shaping",
        "Input range: any\nOutput range: (0 ; unbounded)\nFormula: f(semitones) = rootHz * 2^(semitones/12)",
        { "semitones" }, { 110.0f }, { "rootHz" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = MathHelpers::convertSemitonesToHertz(in[0], p[0]);
        }));

    nodes.push_back(math("math.lookupTable", "Lookup Table", "Lookup Tables",
        "Input range: any\nOutput range: one of the freeform param values\n"
        "Formula: f(index) = params[clamp(round(index), 0, N-1)]", { "index" }, {}, {},
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            if (p.empty()) { out[0] = 0.0f; return; }
            const int idx = juce::jlimit(0, static_cast<int>(p.size()) - 1, static_cast<int>(std::lround(in[0])));
            out[0] = p[static_cast<size_t>(idx)];
        }));
    nodes.push_back(math("math.lut3", "Lookup Table (3 selectors)", "Lookup Tables",
        "Input range: i0/i1/i2 treated as booleans (>=0.5 = 1)\nOutput range: one of the 8 freeform param values\n"
        "Formula: f(i0, i1, i2) = params[bit(i0)*4 + bit(i1)*2 + bit(i2)]",
        { "i0", "i1", "i2" }, std::vector<float>(8, 0.0f), {},
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            if (p.empty()) { out[0] = 0.0f; return; }
            auto bit = [](float v) { return v >= 0.5f ? 1 : 0; };
            const int idx = juce::jlimit(0, static_cast<int>(p.size()) - 1, bit(in[0]) * 4 + bit(in[1]) * 2 + bit(in[2]));
            out[0] = p[static_cast<size_t>(idx)];
        }));

    // Input 1 (rate) is optional - unconnected, it falls back to the rate param instead of a fixed value.
    nodes.push_back(math("math.onePoleSmoother", "One-Pole Smoother", "Dynamics",
        "Input range: any ('rate' input overrides the rate param when connected)\nOutput range: tracks target\n"
        "Formula: f(target, rate) = lerp(prev, target, rate)",
        { "target", "rate" }, { 0.1f }, { "rate" },
        [](const float* in, int, const std::vector<float>&, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            s->a = MathHelpers::applyOnePoleFilter(s->a, in[0], in[1]);
            out[0] = s->a;
        }, true));
    // Also a generator (no graph input) - lives with math.constant under the Source category.
    nodes.push_back(source("math.lfoSine", "LFO (Sine)", "Generator",
        "Free-running sine oscillator at 'rateHz' Hz.\nRange: [-1 ; 1]",
        [](const SourceFrame&, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            constexpr float kTwoPi = 6.28318530717958f;
            s->a += p[0] * kTwoPi / static_cast<float>(s->sampleRate > 0.0 ? s->sampleRate : 44100.0);
            if (s->a >= kTwoPi) s->a -= kTwoPi;
            out[0] = std::sin(s->a);
        },
        { 5.0f }, { "rateHz" }, {}, true));
    nodes.push_back(math("math.leakyIntegrator", "Leaky Integrator", "Dynamics",
        "Input range: any\nOutput range: [0 ; 1]\nFormula: f(add) = clamp(prev * decay + add, 0, 1)",
        { "add" }, { 0.99f }, { "decay" },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            s->a = juce::jlimit(0.0f, 1.0f, s->a * p[0] + in[0]);
            out[0] = s->a;
        }, true));
    nodes.push_back(math("math.retriggerEnvelope", "Retrigger Envelope", "Dynamics",
        "Input range: any\nOutput range: [0 ; 1]\n"
        "Formula: gate crosses above 0.5 -> jump to 1; else out *= decay each block",
        { "gate" }, { 0.9f }, { "decay" },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            const bool above = in[0] > 0.5f;
            if (above && !s->flag) s->a = 1.0f;
            else s->a *= p[0];
            s->flag = above;
            out[0] = s->a;
        }, true));
    nodes.push_back(math("math.hysteresisStep", "Hysteresis Step", "Dynamics",
        "Input range: any\nOutput range: whole numbers\n"
        "Formula: holds round(candidate), updates only once |candidate - held| > width",
        { "candidate" }, { 0.5f }, { "width" },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            if (!s->flag || std::abs(in[0] - s->a) > p[0]) {
                s->a = std::round(in[0]);
                s->flag = true;
            }
            out[0] = s->a;
        }, true));
    nodes.push_back(math("math.derivative", "Derivative", "Dynamics",
        "Input range: any\nOutput range: any (0 on first block)\nFormula: f(value) = value[n] - value[n-1]",
        { "value" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            out[0] = s->flag ? (in[0] - s->a) : 0.0f;
            s->a = in[0];
            s->flag = true;
        }, true));
    nodes.push_back(math("math.latchedSmoother", "Latched Smoother", "Dynamics",
        "Input range: any\nOutput range: tracks target\n"
        "Formula: while gate > 0.5, lerp(prev, target, rate); else hold last output",
        { "target", "gate" }, { 0.1f }, { "rate" },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            if (in[1] > 0.5f) s->a = MathHelpers::applyOnePoleFilter(s->a, in[0], p[0]);
            out[0] = s->a;
        }, true));

    // Performer-facing live displays - their own category (not Math), but evaluated identically
    // (passthrough) so they can be dropped inline on any wire without changing what it carries.
    auto displayPassthrough = [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0]; };
    {
        NodeTypeInfo info = math("display.value", "Value Display", "Monitors",
            "Shows the live input value as a big number.", { "value" }, {}, {}, displayPassthrough);
        info.category = NodeCategory::Display;
        info.displayKind = DisplayKind::Number;
        info.displayDefaultWidth = 120.0f;
        info.displayDefaultHeight = 70.0f;
        nodes.push_back(info);
    }
    {
        NodeTypeInfo info = math("display.meter", "Level Meter", "Monitors",
            "Shows the live input value as a vertical bar, scaled from 'low' to 'high'.",
            { "value" }, { 0.0f, 1.0f }, { "low", "high" }, displayPassthrough);
        info.category = NodeCategory::Display;
        info.displayKind = DisplayKind::Meter;
        info.displayDefaultWidth = 100.0f;
        info.displayDefaultHeight = 130.0f;
        nodes.push_back(info);
    }
    {
        NodeTypeInfo info = math("display.scope", "Scope", "Monitors",
            "Shows a scrolling trace of the live input value over the last few seconds, scaled from "
            "'low' to 'high'.", { "value" }, { 0.0f, 1.0f }, { "low", "high" }, displayPassthrough);
        info.category = NodeCategory::Display;
        info.displayKind = DisplayKind::Scope;
        info.displayDefaultWidth = 220.0f;
        info.displayDefaultHeight = 130.0f;
        nodes.push_back(info);
    }

    // Output Scalar
    nodes.push_back(sink("sink.rootHz", "Root Hz", "Scalar",
        "Synth's root/fundamental frequency; all voices and chord semitones are relative to this.\nRange: [20 ; 2000]",
        20.0f, 2000.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.rootHz = in[0]; }));
    nodes.push_back(sink("sink.numVoices", "Num Voices", "Scalar",
        "How many of the 4 chord voices are active (rounded).\nRange: [0 ; 4]",
        0.0f, 4.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) {
            out.numVoices = juce::jlimit(0, 4, static_cast<int>(std::lround(in[0])));
        }));
    nodes.push_back(sink("sink.masterGain", "Master Gain", "Scalar", "Overall output level.\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.masterGain = in[0]; }));
    nodes.push_back(sink("sink.driveAmt", "Drive Amount", "Scalar",
        "Waveshaping/saturation drive; above ~1 pushes into audible distortion.\nRange: [0 ; 4]",
        0.0f, 4.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.driveAmt = in[0]; }));
    nodes.push_back(sink("sink.vibratoDepth", "Vibrato Depth", "Scalar", "Depth of pitch vibrato.\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.vibratoDepth = in[0]; }));
    nodes.push_back(sink("sink.vibratoRateHz", "Vibrato Rate (Hz)", "Scalar", "Speed of the pitch vibrato LFO.\nRange: [0 ; 20]",
        0.0f, 20.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.vibratoRateHz = in[0]; }));
    nodes.push_back(sink("sink.tremoloDepth", "Tremolo Depth", "Scalar", "Depth of amplitude tremolo.\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.tremoloDepth = in[0]; }));
    nodes.push_back(sink("sink.tremoloRateHz", "Tremolo Rate (Hz)", "Scalar", "Speed of the amplitude tremolo LFO.\nRange: [0 ; 20]",
        0.0f, 20.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.tremoloRateHz = in[0]; }));
    nodes.push_back(sink("sink.noiseAmount", "Noise Amount", "Scalar",
        "Broadband noise mixed into the voices.\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.noiseAmount = in[0]; }));
    nodes.push_back(sink("sink.noiseLpCoef", "Noise LP Coefficient", "Scalar",
        "One-pole lowpass coefficient on the noise before mixing (0=darkest, 1=brightest).\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.noiseLpCoef = in[0]; }));
    nodes.push_back(sink("sink.usePinkNoise", "Pink Noise", "Scalar",
        "Switch: >0.5 colors each voice's noise pink (1/f, weighted toward low frequencies) instead of white "
        "(flat spectrum).\nRange: 0 or 1",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.usePinkNoise = in[0] > 0.5f; }));
    nodes.push_back(sink("sink.lpfCutoffHz", "LPF Cutoff (Hz)", "Scalar", "Cutoff of the output lowpass filter.\nRange: [20 ; 20000]",
        20.0f, 20000.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.lpfCutoffHz = in[0]; }));
    nodes.push_back(sink("sink.useIndependentVoicePitch", "Independent Voice Pitch", "Scalar",
        "Switch: >0.5 makes each voice use its own Voice Hz[i] instead of Root Hz + chord.\nRange: 0 or 1",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.useIndependentVoicePitch = in[0] > 0.5f; }));
    nodes.push_back(sink("sink.reverbWetLevel", "Reverb Wet Level", "Scalar", "Reverb mix, dry to wet.\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbWetLevel = in[0]; }));
    nodes.push_back(sink("sink.reverbRoomSize", "Reverb Room Size", "Scalar", "Reverb decay length ('feedback').\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbRoomSize = in[0]; }));
    nodes.push_back(sink("sink.reverbDamping", "Reverb Damping", "Scalar", "Reverb high-frequency absorption.\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbDamping = in[0]; }));

    // Output Indexed Array
    nodes.push_back(sink("sink.chordSemitone", "Chord Semitone[i]", "Indexed Array",
        "Semitone offset from Root Hz, chord voice 'index' (0-2).\nRange: [-24 ; 24]",
        -24.0f, 24.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.chordSemitones[arrayIndex<3>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.voiceGain", "Voice Gain[i]", "Indexed Array",
        "Per-voice gain, voice 'index' (0-3).\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.voiceGain[arrayIndex<4>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.partialAmp", "Partial Amp[i]", "Indexed Array",
        "Amplitude of harmonic partial 'index' (0-5).\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.partialAmps[arrayIndex<6>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.panL", "Pan L[i]", "Indexed Array",
        "Left-channel gain, voice 'index' (0-3).\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.panL[arrayIndex<4>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.panR", "Pan R[i]", "Indexed Array",
        "Right-channel gain, voice 'index' (0-3).\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.panR[arrayIndex<4>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.voiceHz", "Voice Hz[i]", "Indexed Array",
        "Independent frequency, voice 'index' (0-3); only used when Independent Voice Pitch is on.\nRange: [20 ; 2000]",
        20.0f, 2000.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.voiceHz[arrayIndex<4>(p)] = in[0]; },
        { 0.0f }, { "index" }));

    return nodes;
}

#undef REMORA_RAW_EVAL

} // namespace

void registerAllNodes(NodeTypeRegistry& registry) {
    for (auto& info : buildAllNodes())
        registry.registerType(std::move(info));
}

} // namespace Graph
