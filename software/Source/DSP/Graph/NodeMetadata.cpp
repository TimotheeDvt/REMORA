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
        "Calibrated pitch angle of the staff, in radians. Range: -pi/2 to pi/2 (asin-derived, mathematically "
        "cannot exceed this). Sign/zero-point depend on the calibration pose captured at startup.",
        REMORA_RAW_EVAL(sf.raw.pitch)));
    nodes.push_back(source("source.roll", "Roll", "Raw Sensor",
        "Calibrated roll angle of the staff, in radians. Range: -pi to pi (atan2-derived, mathematically cannot "
        "exceed this). Sign/zero-point depend on the calibration pose captured at startup.",
        REMORA_RAW_EVAL(sf.raw.roll)));
    nodes.push_back(source("source.yaw", "Yaw", "Raw Sensor",
        "Calibrated yaw (heading) angle of the staff, in radians. Range: -pi to pi (atan2-derived, mathematically "
        "cannot exceed this). Sign/zero-point depend on the calibration pose captured at startup.",
        REMORA_RAW_EVAL(sf.raw.yaw)));
    nodes.push_back(source("source.gyroX", "Gyro X", "Raw Sensor",
        "Raw angular velocity around the sensor's X axis, in degrees/second. Signed; no fixed software range - "
        "bounded only by the IMU's hardware full-scale setting. Unfiltered, straight from the IMU.",
        REMORA_RAW_EVAL(sf.raw.gx)));
    nodes.push_back(source("source.gyroY", "Gyro Y", "Raw Sensor",
        "Raw angular velocity around the sensor's Y axis, in degrees/second. Signed; no fixed software range - "
        "bounded only by the IMU's hardware full-scale setting. Unfiltered, straight from the IMU.",
        REMORA_RAW_EVAL(sf.raw.gy)));
    nodes.push_back(source("source.gyroZ", "Gyro Z", "Raw Sensor",
        "Raw angular velocity around the sensor's Z axis, in degrees/second. Signed; no fixed software range - "
        "bounded only by the IMU's hardware full-scale setting. Unfiltered, straight from the IMU.",
        REMORA_RAW_EVAL(sf.raw.gz)));
    nodes.push_back(source("source.accelX", "Accel X", "Raw Sensor",
        "Raw linear acceleration along the sensor's X axis, in g. Signed; no fixed software range - includes "
        "gravity, so roughly -1 to 1 at rest and wider under motion. Includes gravity - not gravity-removed.",
        REMORA_RAW_EVAL(sf.raw.ax)));
    nodes.push_back(source("source.accelY", "Accel Y", "Raw Sensor",
        "Raw linear acceleration along the sensor's Y axis, in g. Signed; no fixed software range - includes "
        "gravity, so roughly -1 to 1 at rest and wider under motion. Includes gravity - not gravity-removed.",
        REMORA_RAW_EVAL(sf.raw.ay)));
    nodes.push_back(source("source.accelZ", "Accel Z", "Raw Sensor",
        "Raw linear acceleration along the sensor's Z axis, in g. Signed; no fixed software range - includes "
        "gravity, so roughly -1 to 1 at rest and wider under motion. Includes gravity - not gravity-removed.",
        REMORA_RAW_EVAL(sf.raw.az)));
    nodes.push_back(source("source.magX", "Mag X", "Raw Sensor",
        "Raw magnetometer reading along the sensor's X axis, in microtesla (uT). Signed; no fixed software "
        "range - depends on the local magnetic environment.", REMORA_RAW_EVAL(sf.raw.mx)));
    nodes.push_back(source("source.magY", "Mag Y", "Raw Sensor",
        "Raw magnetometer reading along the sensor's Y axis, in microtesla (uT). Signed; no fixed software "
        "range - depends on the local magnetic environment.", REMORA_RAW_EVAL(sf.raw.my)));
    nodes.push_back(source("source.magZ", "Mag Z", "Raw Sensor",
        "Raw magnetometer reading along the sensor's Z axis, in microtesla (uT). Signed; no fixed software "
        "range - depends on the local magnetic environment.", REMORA_RAW_EVAL(sf.raw.mz)));
    nodes.push_back(source("source.quatW", "Quat W", "Raw Sensor",
        "W component of the calibrated orientation quaternion. Range: -1 to 1 (unit-quaternion component). "
        "Combine with Quat X/Y/Z to rotate vectors into world frame without reconstructing Euler angles.",
        REMORA_RAW_EVAL(sf.raw.qw)));
    nodes.push_back(source("source.quatX", "Quat X", "Raw Sensor",
        "X component of the calibrated orientation quaternion. Range: -1 to 1 (unit-quaternion component). "
        "Combine with Quat W/Y/Z to rotate vectors into world frame without reconstructing Euler angles.",
        REMORA_RAW_EVAL(sf.raw.qx)));
    nodes.push_back(source("source.quatY", "Quat Y", "Raw Sensor",
        "Y component of the calibrated orientation quaternion. Range: -1 to 1 (unit-quaternion component). "
        "Combine with Quat W/X/Z to rotate vectors into world frame without reconstructing Euler angles.",
        REMORA_RAW_EVAL(sf.raw.qy)));
    nodes.push_back(source("source.quatZ", "Quat Z", "Raw Sensor",
        "Z component of the calibrated orientation quaternion. Range: -1 to 1 (unit-quaternion component). "
        "Combine with Quat W/X/Y to rotate vectors into world frame without reconstructing Euler angles.",
        REMORA_RAW_EVAL(sf.raw.qz)));
    nodes.push_back(source("source.isReceivingValidData", "Valid Data", "Raw Sensor",
        "Range: 0.0 or 1.0 (boolean as float). 1.0 while the staff sensor is sending valid data; 0.0 if the "
        "connection is stale or lost.",
        REMORA_RAW_EVAL(sf.raw.isReceivingValidData ? 1.0f : 0.0f)));

    // Bypasses the shared analyzer - LeadDrone/SpinFilter/BowedChord want instantaneous raw magnitude, not smoothed.
    nodes.push_back(source("source.gyroMagnitudeRaw", "Gyro Magnitude (Raw)", "Raw Sensor",
        "Instantaneous magnitude of the raw gyro vector, in degrees/second (sqrt(gx^2+gy^2+gz^2)). Range: 0 and "
        "up, no fixed upper bound. Unsmoothed - jitters block to block.",
        REMORA_RAW_EVAL(std::sqrt(sf.raw.gx * sf.raw.gx + sf.raw.gy * sf.raw.gy + sf.raw.gz * sf.raw.gz))));
    nodes.push_back(source("source.accelMagnitudeRaw", "Accel Magnitude (Raw)", "Raw Sensor",
        "Instantaneous magnitude of the raw accel vector, in g (sqrt(ax^2+ay^2+az^2)). Range: 0 and up, no fixed "
        "upper bound - typically around 1.0 at rest (gravity) and higher under motion. Includes gravity; "
        "unsmoothed - jitters block to block.",
        REMORA_RAW_EVAL(std::sqrt(sf.raw.ax * sf.raw.ax + sf.raw.ay * sf.raw.ay + sf.raw.az * sf.raw.az))));

    // Derived motion
    nodes.push_back(source("source.gyroMagnitude", "Gyro Magnitude", "Derived Motion",
        "Smoothed gyroscope magnitude, in degrees/second. Range: 0 and up, no fixed upper bound - typically "
        "0-750+; also feeds the isMoving gate (moving once above 30 deg/s).",
        REMORA_RAW_EVAL(sf.derived.smoothedGyroscopeMagnitude)));
    nodes.push_back(source("source.labanWeight", "Laban Weight", "Derived Motion",
        "Laban effort 'Weight' quality: how forceful the motion is. Range: 0.0 to 1.0. 0.0 = light, 1.0 = strong. Derived from integrated dynamic acceleration.",
        REMORA_RAW_EVAL(sf.derived.labanWeight)));
    nodes.push_back(source("source.labanTimeSuddenness", "Laban Time (Suddenness)", "Derived Motion",
        "Laban effort 'Time' quality: how sudden vs sustained the motion is. Range: 0.0 to 1.0. 0.0 = sustained, 1.0 = sudden. Derived from gyro-magnitude jumps.",
        REMORA_RAW_EVAL(sf.derived.labanTimeSuddenness)));
    nodes.push_back(source("source.labanSpaceFocus", "Laban Space Focus", "Derived Motion",
        "Laban effort 'Space' quality: direct vs flexible pathing. Range: 0.0 to 1.0. 0.0 = flexible/indirect, 1.0 = direct/focused.",
        REMORA_RAW_EVAL(sf.derived.labanSpaceFocus)));
    nodes.push_back(source("source.labanFlowBound", "Laban Flow Bound", "Derived Motion",
        "Laban effort 'Flow' quality (bound side): Range: 0.0 to 1.0. 0.0 = free, 1.0 = fully bound/controlled. Derived from jerk (rate of change of acceleration). Roughly complements Laban Flow Free.",
        REMORA_RAW_EVAL(sf.derived.labanFlowBound)));
    nodes.push_back(source("source.labanFlowFree", "Laban Flow Free", "Derived Motion",
        "Laban effort 'Flow' quality (free side): Range: 0.0 to 1.0. 0.0 = bound, 1.0 = fully free/fluid. Roughly complements Laban Flow Bound (the two sum to ~1.0).",
        REMORA_RAW_EVAL(sf.derived.labanFlowFree)));
    nodes.push_back(source("source.thrustPeakEnvelope", "Thrust Peak Envelope", "Derived Motion",
        "Envelope that spikes toward 1.0 on a sharp axial thrust/jab gesture and decays back to 0.0 afterwards. Range: 0.0 to 1.0. 0.0 = no recent thrust.",
        REMORA_RAW_EVAL(sf.derived.axialThrustPeakEnvelope)));
    nodes.push_back(source("source.rotationAxisX", "Rotation Axis X", "Derived Motion",
        "X component of the current (smoothed) world-frame rotation axis. Range: roughly -1 to 1 (unit vector "
        "component). (0, 0) on both X/Y means spinning about the vertical (Z) axis.",
        REMORA_RAW_EVAL(sf.derived.rotationAxisX)));
    nodes.push_back(source("source.rotationAxisY", "Rotation Axis Y", "Derived Motion",
        "Y component of the current (smoothed) world-frame rotation axis. Range: roughly -1 to 1 (unit vector "
        "component). (0, 0) on both X/Y means spinning about the vertical (Z) axis.",
        REMORA_RAW_EVAL(sf.derived.rotationAxisY)));
    nodes.push_back(source("source.deltaTime", "Delta Time (s)", "Derived Motion",
        "Time elapsed since the previous audio block, in seconds. Range: greater than 0, typically well under "
        "0.1s but with no fixed upper bound (can spike on a scheduling stall). Use to make custom per-block "
        "integration/smoothing frame-rate independent.",
        REMORA_RAW_EVAL(sf.derived.deltaTimeSeconds)));
    nodes.push_back(source("source.isMoving", "Is Moving", "Derived Motion",
        "Range: 0.0 or 1.0 (boolean as float). 1.0 if the smoothed gyro magnitude is above the moving threshold (30 deg/s), 0.0 if the staff is considered still.",
        REMORA_RAW_EVAL(sf.derived.isMoving ? 1.0f : 0.0f)));

    // Has side effects (calls back into the live analyzer) since its behavior needs a per-node convention param.
    nodes.push_back(source("source.spinClassification", "Spin Classification", "Derived Motion",
        "Classifies the current spin plane/direction. Only updates while isMoving is true - holds its "
        "last classification while the staff is still.\n"
        "vertical: range 0.0 or 1.0. 1.0 if spinning about a vertical axis, 0.0 if horizontal.\n"
        "spin: range +1.0 or -1.0 (never 0). Spin direction; sign convention is set by the 'convention' param: "
        "0 = by absolute axis component, 1 = by reference azimuth.\n"
        "count: signed integer, no fixed bound. Count of full 360-degree rotations completed in the current "
        "spin plane/direction; resets to 0 whenever the classification changes.\n"
        "facing: range 0.0 or 1.0. 1.0 if the staff tip currently faces the calibrated 'north' direction, 0.0 otherwise.",
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
        "Outputs its 'value' parameter every block, unchanged. Use to feed a fixed number into another node's input.",
        [](const SourceFrame&, const std::vector<float>& p, NodeState*, float* out) { out[0] = p.empty() ? 0.0f : p[0]; },
        { 0.0f }, { "value" }));

    nodes.push_back(math("math.add", "Add", "Arithmetic", "out = a + b.", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0] + in[1]; }));
    nodes.push_back(math("math.subtract", "Subtract", "Arithmetic", "out = a - b.", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0] - in[1]; }));
    nodes.push_back(math("math.multiply", "Multiply", "Arithmetic", "out = a * b.", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0] * in[1]; }));
    nodes.push_back(math("math.divide", "Divide", "Arithmetic",
        "out = a / b. Outputs 0.0 if b is (near) zero, to avoid a divide-by-zero blowup.", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) {
            out[0] = std::abs(in[1]) > 1e-6f ? in[0] / in[1] : 0.0f;
        }));
    nodes.push_back(math("math.abs", "Absolute Value", "Arithmetic", "out = |value|.", { "value" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = std::abs(in[0]); }));
    nodes.push_back(math("math.max", "Max", "Arithmetic", "out = the larger of a and b.", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = juce::jmax(in[0], in[1]); }));
    nodes.push_back(math("math.min", "Min", "Arithmetic", "out = the smaller of a and b.", { "a", "b" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = juce::jmin(in[0], in[1]); }));
    nodes.push_back(math("math.equals", "Equals", "Arithmetic", "out = 1.0 if |a - b| <= epsilon, else 0.0.",
        { "a", "b" }, { 0.5f }, { "epsilon" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = std::abs(in[0] - in[1]) <= p[0] ? 1.0f : 0.0f;
        }));

    nodes.push_back(math("math.mapRange", "Map Range", "Shaping",
        "Linearly remaps value from [inMin, inMax] to [outMin, outMax]. Not clamped - it extrapolates outside "
        "the input range, so add a Clamp node after it if you need hard limits.",
        { "value" }, { 0.0f, 1.0f, 0.0f, 1.0f }, { "inMin", "inMax", "outMin", "outMax" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = juce::jmap(in[0], p[0], p[1], p[2], p[3]);
        }));
    nodes.push_back(math("math.clamp", "Clamp", "Shaping",
        "out = value limited to [min, max] (order-independent - works even if min > max).",
        { "value" }, { 0.0f, 1.0f }, { "min", "max" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = juce::jlimit(juce::jmin(p[0], p[1]), juce::jmax(p[0], p[1]), in[0]);
        }));
    nodes.push_back(math("math.threshold", "Threshold", "Shaping", "out = 1.0 if value > threshold, else 0.0.",
        { "value" }, { 0.0f }, { "threshold" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) { out[0] = in[0] > p[0] ? 1.0f : 0.0f; }));
    nodes.push_back(math("math.crossfade", "Crossfade", "Shaping",
        "out = a when mix=0.0, b when mix=1.0, linearly interpolated in between. mix is clamped to [0, 1].",
        { "a", "b", "mix" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) {
            const float mix = juce::jlimit(0.0f, 1.0f, in[2]);
            out[0] = in[0] + (in[1] - in[0]) * mix;
        }));
    nodes.push_back(math("math.quantizeSteps", "Quantize Steps", "Shaping",
        "Rounds value to the nearest multiple of 'step'. step=0 passes value through unchanged.",
        { "value" }, { 1.0f }, { "step" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            const float step = p[0];
            out[0] = std::abs(step) > 1e-6f ? std::round(in[0] / step) * step : in[0];
        }));
    nodes.push_back(math("math.passthrough", "Passthrough", "Shaping",
        "out = value, unchanged. Useful for tapping one output port of a multi-output node (e.g. Spin "
        "Classification) so it can be wired anywhere a single-output value is expected.",
        { "value" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = in[0]; }));
    nodes.push_back(math("math.sine", "Sine", "Shaping", "out = sin(phase). phase is in radians.", { "phase" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = std::sin(in[0]); }));
    nodes.push_back(math("math.atan2", "Atan2 (y, x)", "Shaping", "out = atan2(y, x), in radians, range -pi..pi.",
        { "y", "x" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState*, float* out) { out[0] = std::atan2(in[0], in[1]); }));
    nodes.push_back(math("math.semitonesToHz", "Semitones to Hz", "Shaping",
        "Converts a semitone offset to a frequency in Hz relative to rootHz (out = rootHz * 2^(semitones/12)).",
        { "semitones" }, { 110.0f }, { "rootHz" },
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            out[0] = MathHelpers::convertSemitonesToHertz(in[0], p[0]);
        }));

    nodes.push_back(math("math.lookupTable", "Lookup Table", "Lookup Tables",
        "Reads the params table at round(index), clamped to the table's bounds, and outputs that value. "
        "Params are freeform - edit each slot's value directly.", { "index" }, {}, {},
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            if (p.empty()) { out[0] = 0.0f; return; }
            const int idx = juce::jlimit(0, static_cast<int>(p.size()) - 1, static_cast<int>(std::lround(in[0])));
            out[0] = p[static_cast<size_t>(idx)];
        }));
    nodes.push_back(math("math.lut3", "Lookup Table (3 selectors)", "Lookup Tables",
        "3-bit lookup table: treats i0/i1/i2 as booleans (>=0.5 = 1) and outputs params[i0*4 + i1*2 + i2] "
        "from the 8-value table.", { "i0", "i1", "i2" }, std::vector<float>(8, 0.0f), {},
        [](const float* in, int, const std::vector<float>& p, NodeState*, float* out) {
            if (p.empty()) { out[0] = 0.0f; return; }
            auto bit = [](float v) { return v >= 0.5f ? 1 : 0; };
            const int idx = juce::jlimit(0, static_cast<int>(p.size()) - 1, bit(in[0]) * 4 + bit(in[1]) * 2 + bit(in[2]));
            out[0] = p[static_cast<size_t>(idx)];
        }));

    // Input 1 (rate) is optional - unconnected, it falls back to the rate param instead of a fixed value.
    nodes.push_back(math("math.onePoleSmoother", "One-Pole Smoother", "Dynamics",
        "Exponentially smooths 'target' toward its new value each block at 'rate' (0..1, higher = faster). "
        "The 'rate' input overrides the rate param when connected; leave it unconnected to use a fixed rate.",
        { "target", "rate" }, { 0.1f }, { "rate" },
        [](const float* in, int, const std::vector<float>&, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            s->a = MathHelpers::applyOnePoleFilter(s->a, in[0], in[1]);
            out[0] = s->a;
        }, true));
    // Also a generator (no graph input) - lives with math.constant under the Source category.
    nodes.push_back(source("math.lfoSine", "LFO (Sine)", "Generator",
        "Free-running sine oscillator at 'rateHz' Hz, output range -1.0..1.0. No input - its phase just keeps "
        "advancing every block.",
        [](const SourceFrame&, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            constexpr float kTwoPi = 6.28318530717958f;
            s->a += p[0] * kTwoPi / static_cast<float>(s->sampleRate > 0.0 ? s->sampleRate : 44100.0);
            if (s->a >= kTwoPi) s->a -= kTwoPi;
            out[0] = std::sin(s->a);
        },
        { 5.0f }, { "rateHz" }, {}, true));
    nodes.push_back(math("math.leakyIntegrator", "Leaky Integrator", "Dynamics",
        "Accumulates 'add' each block, decaying the running total by 'decay' (0..1) first. Output is clamped to [0, 1].",
        { "add" }, { 0.99f }, { "decay" },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            s->a = juce::jlimit(0.0f, 1.0f, s->a * p[0] + in[0]);
            out[0] = s->a;
        }, true));
    nodes.push_back(math("math.retriggerEnvelope", "Retrigger Envelope", "Dynamics",
        "One-shot decay envelope retriggered by 'gate' crossing above 0.5: jumps to 1.0 on the rising edge, "
        "then decays by multiplying by 'decay' (0..1) every subsequent block.", { "gate" }, { 0.9f }, { "decay" },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            const bool above = in[0] > 0.5f;
            if (above && !s->flag) s->a = 1.0f;
            else s->a *= p[0];
            s->flag = above;
            out[0] = s->a;
        }, true));
    nodes.push_back(math("math.hysteresisStep", "Hysteresis Step", "Dynamics",
        "Snaps 'candidate' to the nearest whole number, but only updates its held output once |candidate - held| "
        "exceeds 'width' - reduces chatter around integer boundaries.", { "candidate" }, { 0.5f }, { "width" },
        [](const float* in, int, const std::vector<float>& p, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            if (!s->flag || std::abs(in[0] - s->a) > p[0]) {
                s->a = std::round(in[0]);
                s->flag = true;
            }
            out[0] = s->a;
        }, true));
    nodes.push_back(math("math.derivative", "Derivative", "Dynamics",
        "out = change in 'value' since the previous block (value[n] - value[n-1]). 0.0 on the very first block.",
        { "value" }, {}, {},
        [](const float* in, int, const std::vector<float>&, NodeState* state, float* out) {
            auto* s = static_cast<MathNodeState*>(state);
            out[0] = s->flag ? (in[0] - s->a) : 0.0f;
            s->a = in[0];
            s->flag = true;
        }, true));
    nodes.push_back(math("math.latchedSmoother", "Latched Smoother", "Dynamics",
        "While 'gate' > 0.5, smooths 'target' toward its new value at 'rate' (0..1); holds its last output "
        "whenever gate is <= 0.5.", { "target", "gate" }, { 0.1f }, { "rate" },
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
        "Sets the synth's root/fundamental frequency, in Hz. Wanted input range: 20 Hz to 2000 Hz. All voices and chord semitones are computed relative to this.",
        20.0f, 2000.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.rootHz = in[0]; }));
    nodes.push_back(sink("sink.numVoices", "Num Voices", "Scalar",
        "How many of the 4 chord voices are active, 0-4. Rounded to the nearest whole number and clamped to [0, 4].",
        0.0f, 4.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) {
            out.numVoices = juce::jlimit(0, 4, static_cast<int>(std::lround(in[0])));
        }));
    nodes.push_back(sink("sink.masterGain", "Master Gain", "Scalar", "Overall output level, 0.0 (silent) to 1.0 (full).",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.masterGain = in[0]; }));
    nodes.push_back(sink("sink.driveAmt", "Drive Amount", "Scalar",
        "Amount of waveshaping/saturation drive applied to the voices, 0.0 (clean) upward - values above ~1 push into audible distortion. Wanted input range: 0.0 to 4.0.",
        0.0f, 4.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.driveAmt = in[0]; }));
    nodes.push_back(sink("sink.vibratoDepth", "Vibrato Depth", "Scalar", "Depth of pitch vibrato, 0.0 (none) to 1.0 (max).",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.vibratoDepth = in[0]; }));
    nodes.push_back(sink("sink.vibratoRateHz", "Vibrato Rate (Hz)", "Scalar", "Speed of the pitch vibrato LFO, in Hz. Wanted input range: 0 Hz to 20 Hz.",
        0.0f, 20.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.vibratoRateHz = in[0]; }));
    nodes.push_back(sink("sink.tremoloDepth", "Tremolo Depth", "Scalar", "Depth of amplitude tremolo, 0.0 (none) to 1.0 (max).",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.tremoloDepth = in[0]; }));
    nodes.push_back(sink("sink.tremoloRateHz", "Tremolo Rate (Hz)", "Scalar", "Speed of the amplitude tremolo LFO, in Hz. Wanted input range: 0 Hz to 20 Hz.",
        0.0f, 20.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.tremoloRateHz = in[0]; }));
    nodes.push_back(sink("sink.noiseAmount", "Noise Amount", "Scalar",
        "Amount of broadband noise mixed into the voices, 0.0 (none) to 1.0 (max).",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.noiseAmount = in[0]; }));
    nodes.push_back(sink("sink.noiseLpCoef", "Noise LP Coefficient", "Scalar",
        "One-pole lowpass coefficient applied to the noise before mixing, 0.0 (darkest/most filtered) to 1.0 (brightest/unfiltered).",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.noiseLpCoef = in[0]; }));
    nodes.push_back(sink("sink.lpfCutoffHz", "LPF Cutoff (Hz)", "Scalar", "Cutoff frequency of the output lowpass filter, in Hz. Wanted input range: 20 Hz to 20000 Hz.",
        20.0f, 20000.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.lpfCutoffHz = in[0]; }));
    nodes.push_back(sink("sink.useIndependentVoicePitch", "Independent Voice Pitch", "Scalar",
        "Boolean switch: value > 0.5 makes each voice use its own Voice Hz[i] instead of Root Hz + chord semitones. Wanted input range: 0.0 to 1.0.",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.useIndependentVoicePitch = in[0] > 0.5f; }));
    nodes.push_back(sink("sink.reverbWetLevel", "Reverb Wet Level", "Scalar", "Reverb mix: 0.0 = fully dry/bypassed, 1.0 = fully wet.",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbWetLevel = in[0]; }));
    nodes.push_back(sink("sink.reverbRoomSize", "Reverb Room Size", "Scalar", "Reverb decay length ('feedback'): 0.0 = short, 1.0 = long.",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbRoomSize = in[0]; }));
    nodes.push_back(sink("sink.reverbDamping", "Reverb Damping", "Scalar", "Reverb high-frequency absorption: 0.0 = bright, 1.0 = dark.",
        0.0f, 1.0f, [](const float* in, const std::vector<float>&, MappingOutput& out) { out.reverbDamping = in[0]; }));

    // Output Indexed Array
    nodes.push_back(sink("sink.chordSemitone", "Chord Semitone[i]", "Indexed Array",
        "Semitone offset from Root Hz for chord voice 'index' (0-2), signed. Wanted input range: -24 to 24 semitones. index is rounded and clamped to [0, 2].",
        -24.0f, 24.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.chordSemitones[arrayIndex<3>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.voiceGain", "Voice Gain[i]", "Indexed Array",
        "Per-voice gain for voice 'index' (0-3), 0.0 (silent) to 1.0 (full). index is rounded and clamped to [0, 3].",
        0.0f, 1.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.voiceGain[arrayIndex<4>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.partialAmp", "Partial Amp[i]", "Indexed Array",
        "Amplitude of harmonic partial 'index' (0-5) in the voice timbre, 0.0 (silent) to 1.0 (full). index is rounded and clamped to [0, 5].",
        0.0f, 1.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.partialAmps[arrayIndex<6>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.panL", "Pan L[i]", "Indexed Array",
        "Left-channel gain for voice 'index' (0-3), 0.0 (silent) to 1.0 (full). index is rounded and clamped to [0, 3].",
        0.0f, 1.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.panL[arrayIndex<4>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.panR", "Pan R[i]", "Indexed Array",
        "Right-channel gain for voice 'index' (0-3), 0.0 (silent) to 1.0 (full). index is rounded and clamped to [0, 3].",
        0.0f, 1.0f, [](const float* in, const std::vector<float>& p, MappingOutput& out) { out.panR[arrayIndex<4>(p)] = in[0]; },
        { 0.0f }, { "index" }));
    nodes.push_back(sink("sink.voiceHz", "Voice Hz[i]", "Indexed Array",
        "Independent frequency in Hz for voice 'index' (0-3). Wanted input range: 20 Hz to 2000 Hz. Only used when Independent Voice Pitch is on. index is rounded and clamped to [0, 3].",
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
