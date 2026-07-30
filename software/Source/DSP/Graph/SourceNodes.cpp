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

void registerRawSource(NodeTypeRegistry& registry, const char* id, const char* name, NodeTypeInfo::SourceEvalFn eval) {
    NodeTypeInfo info;
    info.id = id;
    info.displayName = name;
    info.category = NodeCategory::Source;
    info.sourceEval = eval;
    registry.registerType(std::move(info));
}

#define REMORA_RAW_SOURCE(id, name, expr) \
    registerRawSource(registry, id, name, [](const SourceFrame& sf, const std::vector<float>&, NodeState*, float* out) { out[0] = (expr); })

} // namespace

void registerSourceNodes(NodeTypeRegistry& registry) {
    REMORA_RAW_SOURCE("source.pitch", "Pitch", sf.raw.pitch);
    REMORA_RAW_SOURCE("source.roll", "Roll", sf.raw.roll);
    REMORA_RAW_SOURCE("source.yaw", "Yaw", sf.raw.yaw);
    REMORA_RAW_SOURCE("source.gyroX", "Gyro X", sf.raw.gx);
    REMORA_RAW_SOURCE("source.gyroY", "Gyro Y", sf.raw.gy);
    REMORA_RAW_SOURCE("source.gyroZ", "Gyro Z", sf.raw.gz);
    REMORA_RAW_SOURCE("source.accelX", "Accel X", sf.raw.ax);
    REMORA_RAW_SOURCE("source.accelY", "Accel Y", sf.raw.ay);
    REMORA_RAW_SOURCE("source.accelZ", "Accel Z", sf.raw.az);
    REMORA_RAW_SOURCE("source.magX", "Mag X", sf.raw.mx);
    REMORA_RAW_SOURCE("source.magY", "Mag Y", sf.raw.my);
    REMORA_RAW_SOURCE("source.magZ", "Mag Z", sf.raw.mz);
    REMORA_RAW_SOURCE("source.quatW", "Quat W", sf.raw.qw);
    REMORA_RAW_SOURCE("source.quatX", "Quat X", sf.raw.qx);
    REMORA_RAW_SOURCE("source.quatY", "Quat Y", sf.raw.qy);
    REMORA_RAW_SOURCE("source.quatZ", "Quat Z", sf.raw.qz);
    REMORA_RAW_SOURCE("source.isReceivingValidData", "Valid Data", sf.raw.isReceivingValidData ? 1.0f : 0.0f);

    // Several mappings (LeadDrone, SpinFilter, BowedChord) never touch the
    // shared analyzer at all - they compute instantaneous, unsmoothed
    // magnitude directly off the raw sensor fields every block.
    REMORA_RAW_SOURCE("source.gyroMagnitudeRaw", "Gyro Magnitude (Raw)",
        std::sqrt(sf.raw.gx * sf.raw.gx + sf.raw.gy * sf.raw.gy + sf.raw.gz * sf.raw.gz));
    REMORA_RAW_SOURCE("source.accelMagnitudeRaw", "Accel Magnitude (Raw)",
        std::sqrt(sf.raw.ax * sf.raw.ax + sf.raw.ay * sf.raw.ay + sf.raw.az * sf.raw.az));

    REMORA_RAW_SOURCE("source.gyroMagnitude", "Gyro Magnitude", sf.derived.smoothedGyroscopeMagnitude);
    REMORA_RAW_SOURCE("source.labanWeight", "Laban Weight", sf.derived.labanWeight);
    REMORA_RAW_SOURCE("source.labanTimeSuddenness", "Laban Time (Suddenness)", sf.derived.labanTimeSuddenness);
    REMORA_RAW_SOURCE("source.labanSpaceFocus", "Laban Space Focus", sf.derived.labanSpaceFocus);
    REMORA_RAW_SOURCE("source.labanFlowBound", "Laban Flow Bound", sf.derived.labanFlowBound);
    REMORA_RAW_SOURCE("source.labanFlowFree", "Laban Flow Free", sf.derived.labanFlowFree);
    REMORA_RAW_SOURCE("source.thrustPeakEnvelope", "Thrust Peak Envelope", sf.derived.axialThrustPeakEnvelope);
    REMORA_RAW_SOURCE("source.rotationAxisX", "Rotation Axis X", sf.derived.rotationAxisX);
    REMORA_RAW_SOURCE("source.rotationAxisY", "Rotation Axis Y", sf.derived.rotationAxisY);
    REMORA_RAW_SOURCE("source.deltaTime", "Delta Time (s)", sf.derived.deltaTimeSeconds);
    REMORA_RAW_SOURCE("source.isMoving", "Is Moving", sf.derived.isMoving ? 1.0f : 0.0f);

    {
        NodeTypeInfo info;
        info.id = "source.spinClassification";
        info.displayName = "Spin Classification";
        info.category = NodeCategory::Source;
        info.numOutputs = 4; // isVertical, spinDirection, continuousSpinCount, isFacingNorth
        info.defaultParams = { 0.0f }; // 0 = ByAbsoluteComponent, 1 = ByReferenceAzimuth
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
