#include "AzimutCore.h"
#include "PresetHelpers.h"

namespace Graph::Presets {

namespace {

// lut3 selector bits here are [isVertical][isCCW][isEast] (1 = true), which
// is NOT the same bit order as the original kRootSemitoneTable
// [plane_index][spin_index][facing_index] (0 = vertical/CW/north). This
// remaps the original 2x2x2 constant table into an 8-entry flat table
// addressed by our bit convention, once, at graph-build time.
std::vector<float> buildRootLut(const float table[2][2][2]) {
    std::vector<float> lut(8, 0.0f);
    for (int v = 0; v <= 1; ++v) {
        for (int c = 0; c <= 1; ++c) {
            for (int e = 0; e <= 1; ++e) {
                const int planeIdx = v ? 0 : 1;
                const int spinIdx = c ? 1 : 0;
                const int facingIdx = e ? 1 : 0;
                lut[static_cast<size_t>(v * 4 + c * 2 + e)] = table[planeIdx][spinIdx][facingIdx];
            }
        }
    }
    return lut;
}

} // namespace

AzimutCoreOutputs buildAzimutCore(GraphBuilder& b) {
    static const float kRootSemitoneTable[2][2][2] = {
        { { 0.f, 7.f }, { 4.f, 11.f } },
        { { 7.f, 7.f }, { 9.f, 9.f } },
    };
    constexpr float kRootFrequencyHz = 130.81f; // C3
    constexpr float kGyroscopeFloor = 30.0f;    // StaffMotionAnalyzer::kGyroscopeFloor

    AzimutCoreOutputs core{};

    // Spin/facing classification (ByAbsoluteComponent - matches the majority
    // of the Azimut/Bens family).
    NodeId spinClass = b.add("source.spinClassification", { 0.0f });
    NodeId isVertical = tapPort(b, spinClass, 0);
    NodeId spinDirection = tapPort(b, spinClass, 1);
    core.continuousSpinCount = tapPort(b, spinClass, 2);
    NodeId isFacingNorth = tapPort(b, spinClass, 3);

    NodeId isCCW = b.add("math.threshold", { 0.0f });
    b.wire(spinDirection, isCCW);
    NodeId isEast = subNodes(b, constantNode(b, 1.0f), isFacingNorth);

    NodeId targetSemitones = b.add("math.lut3", buildRootLut(kRootSemitoneTable));
    b.wire(isVertical, targetSemitones, 0);
    b.wire(isCCW, targetSemitones, 1);
    b.wire(isEast, targetSemitones, 2);

    NodeId isMoving = b.add("source.isMoving");
    NodeId gyroMag = b.add("source.gyroMagnitude");
    core.gyroMagnitude = gyroMag;

    // Movement-onset envelope boosts the pitch morph speed right after
    // leaving rest, so the root snaps to the new target quickly instead of
    // crawling there at the (slow) resting morph rate.
    NodeId onsetEnv = b.add("math.retriggerEnvelope", { 0.90f });
    b.wire(isMoving, onsetEnv);

    NodeId morphFromSpeed = clampNode(b, scale(b, gyroMag, 1.0f / 2000.0f), 0.005f, 0.2f);
    NodeId onsetBoost = scale(b, onsetEnv, 0.5f);
    NodeId morphSpeed = b.add("math.max");
    b.wire(morphFromSpeed, morphSpeed, 0);
    b.wire(onsetBoost, morphSpeed, 1);

    NodeId currentSemitones = onePoleVariableRate(b, targetSemitones, morphSpeed);
    core.rootHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
    b.wire(currentSemitones, core.rootHz);

    // Octaves of the root instead of a chord, to keep the same pitch class.
    core.chordSemitone[0] = constantNode(b, 12.0f);
    core.chordSemitone[1] = constantNode(b, 7.0f);
    core.chordSemitone[2] = constantNode(b, -12.0f);
    core.numVoices = constantNode(b, 4.0f);

    // melody_gain == is_moving ? 1 : 0, which is exactly source.isMoving.
    core.voiceGain[0] = isMoving;
    core.voiceGain[1] = scale(b, isMoving, 0.8f);
    core.voiceGain[2] = scale(b, isMoving, 0.7f);
    core.voiceGain[3] = scale(b, isMoving, 0.6f);

    NodeId motionGate = standardMotionGate(b, gyroMag, kGyroscopeFloor);

    NodeId labanWeight = b.add("source.labanWeight");
    core.labanWeight = labanWeight;
    NodeId thrustPeak = b.add("source.thrustPeakEnvelope");

    core.masterGain = standardMasterGain(b, motionGate, labanWeight, thrustPeak);

    NodeId peakBrightness = scale(b, thrustPeak, 0.8f);
    core.partialAmp[0] = constantNode(b, 1.0f);
    core.partialAmp[1] = constantNode(b, 0.6f);
    core.partialAmp[2] = constantNode(b, 0.4f);
    core.partialAmp[3] = clampNode(b, addConst(b, scale(b, peakBrightness, 0.5f), 0.3f), 0.0f, 1.0f);
    core.partialAmp[4] = clampNode(b, addConst(b, scale(b, peakBrightness, 0.7f), 0.2f), 0.0f, 1.0f);
    core.partialAmp[5] = clampNode(b, addConst(b, scale(b, peakBrightness, 0.9f), 0.15f), 0.0f, 1.0f);

    NodeId driveAmtBase = addConst(b, labanWeight, 1.0f);
    core.driveAmt = clampNode(b, addNodes(b, driveAmtBase, scale(b, thrustPeak, 2.0f)), 0.0f, 4.0f);

    NodeId suddenness = b.add("source.labanTimeSuddenness");
    NodeId noiseEnv = standardNoiseEnvelope(b, suddenness, thrustPeak, 0.9985f);
    core.noiseAmount = scale(b, noiseEnv, 0.4f);
    NodeId noiseLpInner = addNodes(b, addConst(b, scale(b, suddenness, 0.4f), 0.2f), scale(b, thrustPeak, 0.4f));
    core.noiseLpCoef = subConst(b, noiseLpInner, 1.0f);

    const float panL[4] = { 0.55f, 0.45f, 0.70f, 0.30f };
    const float panR[4] = { 0.45f, 0.55f, 0.30f, 0.70f };
    for (int i = 0; i < 4; ++i) {
        core.panL[i] = constantNode(b, panL[i]);
        core.panR[i] = constantNode(b, panR[i]);
    }

    core.flowBound = b.add("source.labanFlowBound");
    core.flowFree = b.add("source.labanFlowFree");

    return core;
}

NodeId buildSpinCountLpfHz(GraphBuilder& b, const AzimutCoreOutputs& core) {
    NodeId spinPhase = scale(b, core.continuousSpinCount, 1.5f);
    NodeId sineVal = b.add("math.sine");
    b.wire(spinPhase, sineVal);
    NodeId target = b.add("math.mapRange", { -1.0f, 1.0f, 400.0f, 20000.0f });
    b.wire(sineVal, target);
    return onePole(b, target, 0.03f);
}

} // namespace Graph::Presets
