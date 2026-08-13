#include "../IMappingStrategy.h"
#include "../MathHelpers.h"
#include "../SynthManager.h" // full StaffSoundParams definition, used via SourceFrame::raw
#include "NodeMetadata.h"
#include "Presets/SynthPorts.h"
#include <cmath>
#include <cstring>

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
    info.monitorRangeMin = { monitorRangeMin };
    info.monitorRangeMax = { monitorRangeMax };
    info.sinkWrite = write;
    return info;
}

// A sink with one input port per synth parameter, rather than one sink node per parameter - used
// for the per-engine mega-nodes ("Additive Synth", "Granular Synth"). `def` is what the port falls
// back to when left unwired - matching that engine param struct's own default, so a preset that
// doesn't bother wiring e.g. reverb or vibrato gets the same "off" behavior as before.
struct PortSpec { const char* name; float lo; float hi; float def; };
NodeTypeInfo multiSink(const char* id, const char* name, const char* subcategory, const char* description,
                        std::vector<PortSpec> ports, NodeTypeInfo::SinkWriteFn write) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Sink;
    info.subcategory = subcategory;
    info.description = description;
    info.numInputs = static_cast<int>(ports.size());
    int longestNameChars = 0;
    for (const auto& p : ports) {
        info.inputNames.push_back(juce::String(p.name));
        info.monitorRangeMin.push_back(p.lo);
        info.monitorRangeMax.push_back(p.hi);
        info.inputDefaults.push_back(p.def);
        longestNameChars = juce::jmax(longestNameChars, static_cast<int>(std::strlen(p.name)));
    }
    // Wide enough that the longest port name doesn't get cropped (a mega-sink has no output column
    // to share the box with, so this width goes entirely to the input label column).
    info.defaultWidth = 60.0f + static_cast<float>(longestNameChars) * 7.5f;
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

    // The whole synth is one node per engine: every parameter of that engine is a named input
    // port on it, rather than a separate single-purpose sink node per parameter. Overall output
    // level is the only thing shared across engines, so it stays its own tiny sink.
    nodes.push_back(sink("sink.generalGain", "General Gain", "Synth", "Overall output level, shared by every synth engine.\nRange: [0 ; 1]",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.masterGain = in[0]; }));

    nodes.push_back(multiSink("sink.additiveSynth", "Additive Synth", "Synth",
        "The additive/subtractive-hybrid voice engine (4 voices x 6 harmonic partials, drive, noise, "
        "vibrato/tremolo, filter, reverb) - every parameter it exposes, in one node.",
        {
            { "Root Hz", 20.0f, 2000.0f, 110.0f },
            { "Num Voices", 0.0f, 4.0f, 1.0f },
            { "Drive Amount", 0.0f, 4.0f, 0.0f },
            { "Vibrato Depth", 0.0f, 1.0f, 0.0f },
            { "Vibrato Rate (Hz)", 0.0f, 20.0f, 5.0f },
            { "Tremolo Depth", 0.0f, 1.0f, 0.0f },
            { "Tremolo Rate (Hz)", 0.0f, 20.0f, 4.0f },
            { "Noise Amount", 0.0f, 1.0f, 0.0f },
            { "Noise LP Coefficient", 0.0f, 1.0f, 0.5f },
            { "Pink Noise", 0.0f, 1.0f, 0.0f },
            { "LPF Cutoff (Hz)", 20.0f, 20000.0f, 20000.0f },
            { "Independent Voice Pitch", 0.0f, 1.0f, 0.0f },
            { "Reverb Wet Level", 0.0f, 1.0f, 0.0f },
            { "Reverb Room Size", 0.0f, 1.0f, 0.5f },
            { "Reverb Damping", 0.0f, 1.0f, 0.5f },
            { "Chord Semitone 0", -24.0f, 24.0f, 0.0f },
            { "Chord Semitone 1", -24.0f, 24.0f, 0.0f },
            { "Chord Semitone 2", -24.0f, 24.0f, 0.0f },
            { "Voice Gain 0", 0.0f, 1.0f, 1.0f },
            { "Voice Gain 1", 0.0f, 1.0f, 0.0f },
            { "Voice Gain 2", 0.0f, 1.0f, 0.0f },
            { "Voice Gain 3", 0.0f, 1.0f, 0.0f },
            { "Partial Amp 0", 0.0f, 1.0f, 1.0f },
            { "Partial Amp 1", 0.0f, 1.0f, 0.0f },
            { "Partial Amp 2", 0.0f, 1.0f, 0.0f },
            { "Partial Amp 3", 0.0f, 1.0f, 0.0f },
            { "Partial Amp 4", 0.0f, 1.0f, 0.0f },
            { "Partial Amp 5", 0.0f, 1.0f, 0.0f },
            { "Pan L 0", 0.0f, 1.0f, 0.5f },
            { "Pan L 1", 0.0f, 1.0f, 0.5f },
            { "Pan L 2", 0.0f, 1.0f, 0.5f },
            { "Pan L 3", 0.0f, 1.0f, 0.5f },
            { "Pan R 0", 0.0f, 1.0f, 0.5f },
            { "Pan R 1", 0.0f, 1.0f, 0.5f },
            { "Pan R 2", 0.0f, 1.0f, 0.5f },
            { "Pan R 3", 0.0f, 1.0f, 0.5f },
            { "Voice Hz 0", 20.0f, 2000.0f, 110.0f },
            { "Voice Hz 1", 20.0f, 2000.0f, 110.0f },
            { "Voice Hz 2", 20.0f, 2000.0f, 110.0f },
            { "Voice Hz 3", 20.0f, 2000.0f, 110.0f },
        },
        [](const float* in, const std::vector<float>&, MappingOutput& out) {
            using namespace AdditivePort;
            out.additiveActive = true;
            AdditiveSynthParams& a = out.additive;
            a.rootHz = in[RootHz];
            a.numVoices = juce::jlimit(0, 4, static_cast<int>(std::lround(in[NumVoices])));
            a.driveAmt = in[DriveAmt];
            a.vibratoDepth = in[VibratoDepth];
            a.vibratoRateHz = in[VibratoRateHz];
            a.tremoloDepth = in[TremoloDepth];
            a.tremoloRateHz = in[TremoloRateHz];
            a.noiseAmount = in[NoiseAmount];
            a.noiseLpCoef = in[NoiseLpCoef];
            a.usePinkNoise = in[UsePinkNoise] > 0.5f;
            a.lpfCutoffHz = in[LpfCutoffHz];
            a.useIndependentVoicePitch = in[UseIndependentVoicePitch] > 0.5f;
            a.reverbWetLevel = in[ReverbWetLevel];
            a.reverbRoomSize = in[ReverbRoomSize];
            a.reverbDamping = in[ReverbDamping];
            for (int i = 0; i < 3; ++i) a.chordSemitones[i] = in[ChordSemitone0 + i];
            for (int i = 0; i < 4; ++i) a.voiceGain[i] = in[VoiceGain0 + i];
            for (int i = 0; i < 6; ++i) a.partialAmps[i] = in[PartialAmp0 + i];
            for (int i = 0; i < 4; ++i) a.panL[i] = in[PanL0 + i];
            for (int i = 0; i < 4; ++i) a.panR[i] = in[PanR0 + i];
            for (int i = 0; i < 4; ++i) a.voiceHz[i] = in[VoiceHz0 + i];
        }));

    nodes.push_back(multiSink("sink.granularSynth", "Granular Synth", "Synth",
        "The grain-cloud engine, scanning an internal evolving wavetable - every parameter it "
        "exposes, in one node. Level defaults to 0 (silent) so unused presets stay quiet.",
        {
            { "Position", 0.0f, 1.0f, 0.0f },
            { "Position Spray", 0.0f, 1.0f, 0.0f },
            { "Grain Size (ms)", 5.0f, 500.0f, 60.0f },
            { "Density (Hz)", 1.0f, 200.0f, 20.0f },
            { "Pitch (semitones)", -24.0f, 24.0f, 0.0f },
            { "Pitch Spray", 0.0f, 12.0f, 0.0f },
            { "Pan Spread", 0.0f, 1.0f, 0.0f },
            { "Amp Spray", 0.0f, 1.0f, 0.0f },
            { "Level", 0.0f, 1.0f, 0.0f },
            { "Reverse Amount", 0.0f, 1.0f, 0.0f },
        },
        [](const float* in, const std::vector<float>&, MappingOutput& out) {
            using namespace GranularPort;
            out.granularActive = true;
            GranularSynthParams& g = out.granular;
            g.positionNorm = in[Position];
            g.positionSpray = in[PositionSpray];
            g.grainSizeMs = in[GrainSizeMs];
            g.densityHz = in[DensityHz];
            g.pitchSemitones = in[PitchSemitones];
            g.pitchSpray = in[PitchSpray];
            g.panSpread = in[PanSpread];
            g.ampSpray = in[AmpSpray];
            g.level = in[Level];
            g.reverseAmount = in[ReverseAmount];
        }));

    nodes.push_back(multiSink("sink.pinkTromboneSynth", "Pink Trombone", "Synth",
        "Vocal tract physical model (LF-model glottal source into a digital-waveguide vocal tract, "
        "oral + nasal branches) - every parameter it exposes, in one node. Level defaults to 0 "
        "(silent) so unused presets stay quiet.",
        {
            { "Frequency (Hz)", 40.0f, 600.0f, 140.0f },
            { "Tenseness", 0.0f, 1.0f, 0.6f },
            { "Tongue Position", 0.0f, 1.0f, 0.5f },
            { "Tongue Height", 1.5f, 3.5f, 2.75f },
            { "Constriction Position", 0.0f, 1.0f, 0.5f },
            { "Constriction Diameter", 0.0f, 4.0f, 4.0f },
            { "Fricative Intensity", 0.0f, 1.0f, 0.0f },
            { "Level", 0.0f, 1.0f, 0.0f },
        },
        [](const float* in, const std::vector<float>&, MappingOutput& out) {
            using namespace PinkTrombonePort;
            out.pinkTromboneActive = true;
            PinkTromboneParams& v = out.pinkTrombone;
            v.frequencyHz = in[FrequencyHz];
            v.tenseness = in[Tenseness];
            v.tongueIndexNorm = in[TongueIndexNorm];
            v.tongueDiameter = in[TongueDiameter];
            v.constrictionIndexNorm = in[ConstrictionIndexNorm];
            v.constrictionDiameter = in[ConstrictionDiameter];
            v.fricativeIntensity = in[FricativeIntensity];
            v.level = in[Level];
        }));

    return nodes;
}

#undef REMORA_RAW_EVAL

} // namespace

void registerAllNodes(NodeTypeRegistry& registry) {
    for (auto& info : buildAllNodes())
        registry.registerType(std::move(info));
}

} // namespace Graph
