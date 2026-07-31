#include "../BoStaffSynth.h"
#include "NodeTypeRegistry.h"
#include <cmath>

// Source node eval functions. Raw/derived nodes are plain field reads off
// the SourceFrame snapshot computed once per block by
// StaffMotionAnalyzer::computeFrame(). The one exception is
// source.spinClassification, which has side effects (it calls back into the
// live analyzer) because its behavior depends on a per-node "which
// convention" parameter that computeFrame() can't bake in - see
// StaffMotionAnalyzer.h's DerivedMotionFrame comment.

namespace Graph {
namespace {

void registerRawSource(NodeTypeRegistry& registry, const char* id, const char* name, const char* subcategory,
                        const char* description, NodeTypeInfo::SourceEvalFn eval) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Source;
    info.subcategory = subcategory;
    info.description = description;
    info.sourceEval = eval;
    registry.registerType(std::move(info));
}

#define REMORA_RAW_SOURCE(id, name, subcategory, description, expr) \
    registerRawSource(registry, id, name, subcategory, description, [](const SourceFrame& sf, const std::vector<float>&, NodeState*, float* out) { out[0] = (expr); })

} // namespace

void registerSourceNodes(NodeTypeRegistry& registry) {
    REMORA_RAW_SOURCE("source.pitch", "Pitch", "Raw Sensor",
        "Calibrated pitch angle of the staff, in degrees. Sign/zero-point depend on the calibration pose captured at startup.",
        sf.raw.pitch);
    REMORA_RAW_SOURCE("source.roll", "Roll", "Raw Sensor",
        "Calibrated roll angle of the staff, in degrees. Sign/zero-point depend on the calibration pose captured at startup.",
        sf.raw.roll);
    REMORA_RAW_SOURCE("source.yaw", "Yaw", "Raw Sensor",
        "Calibrated yaw (heading) angle of the staff, in degrees. Sign/zero-point depend on the calibration pose captured at startup.",
        sf.raw.yaw);
    REMORA_RAW_SOURCE("source.gyroX", "Gyro X", "Raw Sensor",
        "Raw angular velocity around the sensor's X axis, in degrees/second. Unfiltered, straight from the IMU.",
        sf.raw.gx);
    REMORA_RAW_SOURCE("source.gyroY", "Gyro Y", "Raw Sensor",
        "Raw angular velocity around the sensor's Y axis, in degrees/second. Unfiltered, straight from the IMU.",
        sf.raw.gy);
    REMORA_RAW_SOURCE("source.gyroZ", "Gyro Z", "Raw Sensor",
        "Raw angular velocity around the sensor's Z axis, in degrees/second. Unfiltered, straight from the IMU.",
        sf.raw.gz);
    REMORA_RAW_SOURCE("source.accelX", "Accel X", "Raw Sensor",
        "Raw linear acceleration along the sensor's X axis, in g. Includes gravity - not gravity-removed.",
        sf.raw.ax);
    REMORA_RAW_SOURCE("source.accelY", "Accel Y", "Raw Sensor",
        "Raw linear acceleration along the sensor's Y axis, in g. Includes gravity - not gravity-removed.",
        sf.raw.ay);
    REMORA_RAW_SOURCE("source.accelZ", "Accel Z", "Raw Sensor",
        "Raw linear acceleration along the sensor's Z axis, in g. Includes gravity - not gravity-removed.",
        sf.raw.az);
    REMORA_RAW_SOURCE("source.magX", "Mag X", "Raw Sensor",
        "Raw magnetometer reading along the sensor's X axis, in microtesla (uT).",
        sf.raw.mx);
    REMORA_RAW_SOURCE("source.magY", "Mag Y", "Raw Sensor",
        "Raw magnetometer reading along the sensor's Y axis, in microtesla (uT).",
        sf.raw.my);
    REMORA_RAW_SOURCE("source.magZ", "Mag Z", "Raw Sensor",
        "Raw magnetometer reading along the sensor's Z axis, in microtesla (uT).",
        sf.raw.mz);
    REMORA_RAW_SOURCE("source.quatW", "Quat W", "Raw Sensor",
        "W component of the calibrated orientation quaternion. Combine with Quat X/Y/Z to rotate vectors into world frame without reconstructing Euler angles.",
        sf.raw.qw);
    REMORA_RAW_SOURCE("source.quatX", "Quat X", "Raw Sensor",
        "X component of the calibrated orientation quaternion. Combine with Quat W/Y/Z to rotate vectors into world frame without reconstructing Euler angles.",
        sf.raw.qx);
    REMORA_RAW_SOURCE("source.quatY", "Quat Y", "Raw Sensor",
        "Y component of the calibrated orientation quaternion. Combine with Quat W/X/Z to rotate vectors into world frame without reconstructing Euler angles.",
        sf.raw.qy);
    REMORA_RAW_SOURCE("source.quatZ", "Quat Z", "Raw Sensor",
        "Z component of the calibrated orientation quaternion. Combine with Quat W/X/Y to rotate vectors into world frame without reconstructing Euler angles.",
        sf.raw.qz);
    REMORA_RAW_SOURCE("source.isReceivingValidData", "Valid Data", "Raw Sensor",
        "1.0 while the staff sensor is sending valid data; 0.0 if the connection is stale or lost.",
        sf.raw.isReceivingValidData ? 1.0f : 0.0f);

    // Several mappings (LeadDrone, SpinFilter, BowedChord) never touch the
    // shared analyzer at all - they compute instantaneous, unsmoothed
    // magnitude directly off the raw sensor fields every block.
    REMORA_RAW_SOURCE("source.gyroMagnitudeRaw", "Gyro Magnitude (Raw)", "Raw Sensor",
        "Instantaneous magnitude of the raw gyro vector, in degrees/second (sqrt(gx^2+gy^2+gz^2)). Unsmoothed - jitters block to block.",
        std::sqrt(sf.raw.gx * sf.raw.gx + sf.raw.gy * sf.raw.gy + sf.raw.gz * sf.raw.gz));
    REMORA_RAW_SOURCE("source.accelMagnitudeRaw", "Accel Magnitude (Raw)", "Raw Sensor",
        "Instantaneous magnitude of the raw accel vector, in g (sqrt(ax^2+ay^2+az^2)). Includes gravity; unsmoothed - jitters block to block.",
        std::sqrt(sf.raw.ax * sf.raw.ax + sf.raw.ay * sf.raw.ay + sf.raw.az * sf.raw.az));

    REMORA_RAW_SOURCE("source.gyroMagnitude", "Gyro Magnitude", "Derived Motion",
        "Smoothed gyroscope magnitude, in degrees/second. Roughly 0-750+; also feeds the isMoving gate (moving once above 30 deg/s).",
        sf.derived.smoothedGyroscopeMagnitude);
    REMORA_RAW_SOURCE("source.labanWeight", "Laban Weight", "Derived Motion",
        "Laban effort 'Weight' quality: how forceful the motion is. 0.0 = light, 1.0 = strong. Derived from integrated dynamic acceleration.",
        sf.derived.labanWeight);
    REMORA_RAW_SOURCE("source.labanTimeSuddenness", "Laban Time (Suddenness)", "Derived Motion",
        "Laban effort 'Time' quality: how sudden vs sustained the motion is. 0.0 = sustained, 1.0 = sudden. Derived from gyro-magnitude jumps.",
        sf.derived.labanTimeSuddenness);
    REMORA_RAW_SOURCE("source.labanSpaceFocus", "Laban Space Focus", "Derived Motion",
        "Laban effort 'Space' quality: direct vs flexible pathing. 0.0 = flexible/indirect, 1.0 = direct/focused.",
        sf.derived.labanSpaceFocus);
    REMORA_RAW_SOURCE("source.labanFlowBound", "Laban Flow Bound", "Derived Motion",
        "Laban effort 'Flow' quality (bound side): 0.0 = free, 1.0 = fully bound/controlled. Derived from jerk (rate of change of acceleration). Roughly complements Laban Flow Free.",
        sf.derived.labanFlowBound);
    REMORA_RAW_SOURCE("source.labanFlowFree", "Laban Flow Free", "Derived Motion",
        "Laban effort 'Flow' quality (free side): 0.0 = bound, 1.0 = fully free/fluid. Roughly complements Laban Flow Bound (the two sum to ~1.0).",
        sf.derived.labanFlowFree);
    REMORA_RAW_SOURCE("source.thrustPeakEnvelope", "Thrust Peak Envelope", "Derived Motion",
        "Envelope that spikes toward 1.0 on a sharp axial thrust/jab gesture and decays back to 0.0 afterwards. 0.0 = no recent thrust.",
        sf.derived.axialThrustPeakEnvelope);
    REMORA_RAW_SOURCE("source.rotationAxisX", "Rotation Axis X", "Derived Motion",
        "X component of the current (smoothed) world-frame rotation axis, roughly -1..1. (0, 0) on both X/Y means spinning about the vertical (Z) axis.",
        sf.derived.rotationAxisX);
    REMORA_RAW_SOURCE("source.rotationAxisY", "Rotation Axis Y", "Derived Motion",
        "Y component of the current (smoothed) world-frame rotation axis, roughly -1..1. (0, 0) on both X/Y means spinning about the vertical (Z) axis.",
        sf.derived.rotationAxisY);
    REMORA_RAW_SOURCE("source.deltaTime", "Delta Time (s)", "Derived Motion",
        "Time elapsed since the previous audio block, in seconds. Use to make custom per-block integration/smoothing frame-rate independent.",
        sf.derived.deltaTimeSeconds);
    REMORA_RAW_SOURCE("source.isMoving", "Is Moving", "Derived Motion",
        "1.0 if the smoothed gyro magnitude is above the moving threshold (30 deg/s), 0.0 if the staff is considered still. Not true/false text - a float 1.0/0.0.",
        sf.derived.isMoving ? 1.0f : 0.0f);

    {
        NodeTypeInfo info;
        info.id = "source.spinClassification";
        info.displayName = "Spin Classification";
        info.category = NodeCategory::Source;
        info.subcategory = "Derived Motion";
        info.numOutputs = 4; // isVertical, spinDirection, continuousSpinCount, isFacingNorth
        info.outputNames = { "vertical", "spin", "count", "facing" };
        info.defaultParams = { 0.0f }; // 0 = ByAbsoluteComponent, 1 = ByReferenceAzimuth
        info.paramNames = { "convention" };
        info.description =
            "Classifies the current spin plane/direction. Only updates while isMoving is true - holds its "
            "last classification while the staff is still.\n"
            "vertical: 1.0 if spinning about a vertical axis, 0.0 if horizontal.\n"
            "spin: spin direction, +1.0 or -1.0 (never 0). Sign convention is set by the 'convention' param: "
            "0 = by absolute axis component, 1 = by reference azimuth.\n"
            "count: signed count of full 360-degree rotations completed in the current spin plane/direction; "
            "resets to 0 whenever the classification changes.\n"
            "facing: 1.0 if the staff tip currently faces the calibrated 'north' direction, 0.0 otherwise.";
        info.sourceEval = [](const SourceFrame& sf, const std::vector<float>& params, NodeState*, float* out) {
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
        };
        registry.registerType(std::move(info));
    }
}

#undef REMORA_RAW_SOURCE

} // namespace Graph
