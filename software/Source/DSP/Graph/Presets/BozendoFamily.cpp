#include "AllPresets.h"
#include "PresetHelpers.h"
#include <vector>

// BozendoMapping and BozendoMapping2 both use the ByReferenceAzimuth spin
// convention, but otherwise differ enough (pitch/chord logic, voice gain,
// modulation, pan) that they don't share a "core" the way the Azimut family
// does - each is built independently here, mirroring how the original C++
// files duplicated the shared formulas (masterGain, noise envelope,
// peak-boosted timbre - factored into PresetHelpers.h) rather than sharing
// a base class.
namespace Graph::Presets {

namespace {

NodeId lut3Node(GraphBuilder& b, NodeId in0, NodeId in1, NodeId in2, std::vector<float> table) {
    NodeId n = b.add("math.lut3", std::move(table));
    b.wire(in0, n, 0);
    b.wire(in1, n, 1);
    b.wire(in2, n, 2);
    return n;
}

} // namespace

std::unique_ptr<NodeGraph> buildBozendo() {
    constexpr float kGyroscopeFloor = 30.0f, kGyroscopeCeiling = 750.0f;
    constexpr float kRootFrequencyHz = 146.83f;
    constexpr float kPi = 3.14159265f;
    static const float kPentatonicMinorScale[10] = { -12.f, -9.f, -5.f, -2.f, 0.f, 3.f, 7.f, 10.f, 12.f, 15.f };
    static const float kDirectGain[6] = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
    static const float kIndirectGain[6] = { 1.0f, 0.10f, 0.50f, 0.05f, 0.30f, 0.03f };
    // Chord quality lut3, selectors [isVertical][isCCW][unused=0]:
    // horiz+CW=Minor{3,7,12}, horiz+CCW=Major{4,7,12}, vert+CW=Dim7{3,6,9}, vert+CCW=7th{4,7,10}
    static const float kChordCol0[8] = { 3, 0, 4, 0, 3, 0, 4, 0 };
    static const float kChordCol1[8] = { 7, 0, 7, 0, 6, 0, 7, 0 };
    static const float kChordCol2[8] = { 12, 0, 12, 0, 9, 0, 10, 0 };

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    NodeId spinClass = b.add("source.spinClassification", { 1.0f }); // ByReferenceAzimuth
    NodeId isVertical = tapPort(b, spinClass, 0);
    NodeId spinDirection = tapPort(b, spinClass, 1);
    NodeId isCCW = threshold(b, spinDirection, 0.0f);
    NodeId zero = constantNode(b, 0.0f);

    NodeId gyroMag = b.add("source.gyroMagnitude");

    // Schmitt-trigger scale-step selector: candidateStep in [0,9], only
    // moves once it drifts more than 0.6 steps from the currently-held step.
    NodeId gyroClamped = clampNode(b, gyroMag, kGyroscopeFloor, kGyroscopeCeiling);
    NodeId candidateStep = b.add("math.mapRange", { kGyroscopeFloor, kGyroscopeCeiling, 0.0f, 9.0f });
    b.wire(gyroClamped, candidateStep);
    NodeId currentStep = b.add("math.hysteresisStep", { 0.6f });
    b.wire(candidateStep, currentStep);
    NodeId baseSemitones = b.add("math.lookupTable", std::vector<float>(kPentatonicMinorScale, kPentatonicMinorScale + 10));
    b.wire(currentStep, baseSemitones);

    NodeId planeOffset = scale(b, isVertical, 12.0f);
    NodeId directionOffset = scale(b, subNodes(b, constantNode(b, 1.0f), isCCW), -7.0f);
    NodeId semitones = addNodes(b, addNodes(b, baseSemitones, planeOffset), directionOffset);
    NodeId rootHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
    b.wire(semitones, rootHz);
    toSink(b, rootHz, "sink.rootHz");

    toSink(b, lut3Node(b, isVertical, isCCW, zero, std::vector<float>(kChordCol0, kChordCol0 + 8)), "sink.chordSemitone", { 0.0f });
    toSink(b, lut3Node(b, isVertical, isCCW, zero, std::vector<float>(kChordCol1, kChordCol1 + 8)), "sink.chordSemitone", { 1.0f });
    toSink(b, lut3Node(b, isVertical, isCCW, zero, std::vector<float>(kChordCol2, kChordCol2 + 8)), "sink.chordSemitone", { 2.0f });

    NodeId labanWeight = b.add("source.labanWeight");
    NodeId isMoving = b.add("source.isMoving");
    NodeId thrustPeak = b.add("source.thrustPeakEnvelope");

    NodeId voiceLadder = addConst(b, addNodes(b, threshold(b, labanWeight, 0.25f),
        addNodes(b, threshold(b, labanWeight, 0.55f), threshold(b, labanWeight, 0.80f))), 1.0f);
    toSink(b, voiceLadder, "sink.numVoices");

    NodeId melodyGain = mulNodes(b, isMoving, clampNode(b, addConst(b, scale(b, gyroMag, 0.8f / kGyroscopeCeiling), 0.2f), 0.2f, 1.0f));
    toSink(b, melodyGain, "sink.voiceGain", { 0.0f });
    toSink(b, scale(b, clampNode(b, scale(b, subNodes(b, labanWeight, constantNode(b, 0.25f)), 4.0f), 0.0f, 1.0f), 0.8f), "sink.voiceGain", { 1.0f });
    toSink(b, scale(b, clampNode(b, scale(b, subNodes(b, labanWeight, constantNode(b, 0.55f)), 3.3f), 0.0f, 1.0f), 0.7f), "sink.voiceGain", { 2.0f });
    toSink(b, scale(b, clampNode(b, scale(b, subNodes(b, labanWeight, constantNode(b, 0.80f)), 5.0f), 0.0f, 1.0f), 0.6f), "sink.voiceGain", { 3.0f });

    NodeId motionGate = standardMotionGate(b, gyroMag, kGyroscopeFloor);
    toSink(b, standardMasterGain(b, motionGate, labanWeight, thrustPeak), "sink.masterGain");

    NodeId spaceFocus = b.add("source.labanSpaceFocus");
    NodeId peakBrightness = scale(b, thrustPeak, 0.8f);
    for (int i = 0; i < 6; ++i) {
        NodeId blended = b.add("math.crossfade");
        b.wire(constantNode(b, kIndirectGain[i]), blended, 0);
        b.wire(constantNode(b, kDirectGain[i]), blended, 1);
        b.wire(spaceFocus, blended, 2);
        NodeId value = blended;
        if (i >= 3) {
            const float peakCoeff = (i == 3) ? 0.5f : (i == 4) ? 0.7f : 0.9f;
            value = clampNode(b, addNodes(b, blended, scale(b, peakBrightness, peakCoeff)), 0.0f, 1.0f);
        }
        toSink(b, value, "sink.partialAmp", { static_cast<float>(i) });
    }

    NodeId driveInner = subConst(b, scale(b, spaceFocus, 0.7f), 1.0f);
    NodeId driveBase = scale(b, mulNodes(b, labanWeight, driveInner), 2.5f);
    toSink(b, clampNode(b, addNodes(b, driveBase, scale(b, thrustPeak, 2.0f)), 0.0f, 4.0f), "sink.driveAmt");

    NodeId suddenness = b.add("source.labanTimeSuddenness");
    NodeId noiseEnv = standardNoiseEnvelope(b, suddenness, thrustPeak, 0.9985f);
    toSink(b, scale(b, noiseEnv, 0.4f), "sink.noiseAmount");
    toSink(b, subConst(b, addNodes(b, addConst(b, scale(b, suddenness, 0.4f), 0.2f), scale(b, thrustPeak, 0.4f)), 1.0f), "sink.noiseLpCoef");

    NodeId flowBound = b.add("source.labanFlowBound");
    NodeId flowFree = b.add("source.labanFlowFree");
    toSink(b, scale(b, mulNodes(b, flowFree, labanWeight), 0.020f), "sink.vibratoDepth");
    toSink(b, addConst(b, scale(b, gyroMag, 0.005f), 4.5f), "sink.vibratoRateHz");
    toSink(b, scale(b, mulNodes(b, flowBound, labanWeight), 0.30f), "sink.tremoloDepth");
    toSink(b, addConst(b, scale(b, flowBound, 4.0f), 3.0f), "sink.tremoloRateHz");

    // pan_bias = |axis| > 1e-3 ? clamp(atan2(axisY,axisX)/pi * 0.2, -0.2, 0.2) : 0
    // (gated via magnitude-squared > 1e-6 instead of sqrt-then-compare).
    NodeId axisX = b.add("source.rotationAxisX");
    NodeId axisY = b.add("source.rotationAxisY");
    NodeId magSqGate = threshold(b, addNodes(b, mulNodes(b, axisX, axisX), mulNodes(b, axisY, axisY)), 1e-6f);
    NodeId azimuth = b.add("math.atan2");
    b.wire(axisY, azimuth, 0);
    b.wire(axisX, azimuth, 1);
    NodeId panBias = mulNodes(b, clampNode(b, scale(b, azimuth, 0.2f / kPi), -0.2f, 0.2f), magSqGate);
    NodeId negPanBias = scale(b, panBias, -1.0f);

    NodeId spread = addConst(b, scale(b, flowFree, 0.5f), 0.3f);
    const float coeffL[4] = { 0.10f, -0.10f, 0.40f, -0.40f };
    for (int i = 0; i < 4; ++i) {
        toSink(b, clampNode(b, addNodes(b, addConst(b, scale(b, spread, coeffL[i]), 0.5f), panBias), 0.0f, 1.0f), "sink.panL", { static_cast<float>(i) });
        toSink(b, clampNode(b, addNodes(b, addConst(b, scale(b, spread, -coeffL[i]), 0.5f), negPanBias), 0.0f, 1.0f), "sink.panR", { static_cast<float>(i) });
    }

    return graph;
}

std::unique_ptr<NodeGraph> buildBozendo2() {
    constexpr float kRootFrequencyHz = 130.81f; // C3

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    NodeId spinClass = b.add("source.spinClassification", { 1.0f }); // ByReferenceAzimuth
    NodeId isVertical = tapPort(b, spinClass, 0);
    NodeId spinDirection = tapPort(b, spinClass, 1);
    NodeId continuousSpinCount = tapPort(b, spinClass, 2);
    NodeId isCCW = threshold(b, spinDirection, 0.0f);

    // lut3 index = isVertical*4 + isCCW*2: idx0=horiz+CW=G(7), idx2=horiz+CCW=A(9),
    // idx4=vert+CW=C(0), idx6=vert+CCW=E(4) - matches the original's if/else exactly.
    NodeId targetSemitones = lut3Node(b, isVertical, isCCW, constantNode(b, 0.0f),
        { 7.0f, 0.0f, 9.0f, 0.0f, 0.0f, 0.0f, 4.0f, 0.0f });

    NodeId gyroMag = b.add("source.gyroMagnitude");
    NodeId morphSpeed = clampNode(b, scale(b, gyroMag, 1.0f / 2000.0f), 0.005f, 0.2f);
    NodeId currentSemitones = onePoleVariableRate(b, targetSemitones, morphSpeed);
    NodeId rootHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
    b.wire(currentSemitones, rootHz);
    toSink(b, rootHz, "sink.rootHz");

    toSink(b, constantNode(b, 12.0f), "sink.chordSemitone", { 0.0f });
    toSink(b, constantNode(b, 7.0f), "sink.chordSemitone", { 1.0f });
    toSink(b, constantNode(b, -12.0f), "sink.chordSemitone", { 2.0f });
    toSink(b, constantNode(b, 4.0f), "sink.numVoices");

    NodeId isMoving = b.add("source.isMoving");
    toSink(b, isMoving, "sink.voiceGain", { 0.0f });
    toSink(b, scale(b, isMoving, 0.8f), "sink.voiceGain", { 1.0f });
    toSink(b, scale(b, isMoving, 0.7f), "sink.voiceGain", { 2.0f });
    toSink(b, scale(b, isMoving, 0.6f), "sink.voiceGain", { 3.0f });

    constexpr float kGyroscopeFloor = 30.0f;
    NodeId labanWeight = b.add("source.labanWeight");
    NodeId thrustPeak = b.add("source.thrustPeakEnvelope");
    NodeId motionGate = standardMotionGate(b, gyroMag, kGyroscopeFloor);
    toSink(b, standardMasterGain(b, motionGate, labanWeight, thrustPeak), "sink.masterGain");

    NodeId peakBrightness = scale(b, thrustPeak, 0.8f);
    static const float kConstantGain[6] = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
    for (int i = 0; i < 6; ++i) {
        NodeId value = constantNode(b, kConstantGain[i]);
        if (i >= 3) {
            const float peakCoeff = (i == 3) ? 0.5f : (i == 4) ? 0.7f : 0.9f;
            value = clampNode(b, addConst(b, scale(b, peakBrightness, peakCoeff), kConstantGain[i]), 0.0f, 1.0f);
        }
        toSink(b, value, "sink.partialAmp", { static_cast<float>(i) });
    }
    NodeId driveBase = addConst(b, labanWeight, 1.0f);
    toSink(b, clampNode(b, addNodes(b, driveBase, scale(b, thrustPeak, 2.0f)), 0.0f, 4.0f), "sink.driveAmt");

    NodeId suddenness = b.add("source.labanTimeSuddenness");
    NodeId noiseEnv = standardNoiseEnvelope(b, suddenness, thrustPeak, 0.9985f);
    toSink(b, scale(b, noiseEnv, 0.4f), "sink.noiseAmount");
    toSink(b, subConst(b, addNodes(b, addConst(b, scale(b, suddenness, 0.4f), 0.2f), scale(b, thrustPeak, 0.4f)), 1.0f), "sink.noiseLpCoef");

    toSink(b, constantNode(b, 0.0f), "sink.vibratoDepth");
    toSink(b, constantNode(b, 0.0f), "sink.vibratoRateHz");
    toSink(b, constantNode(b, 0.0f), "sink.tremoloDepth");
    toSink(b, constantNode(b, 0.0f), "sink.tremoloRateHz");

    const float panL[4] = { 0.55f, 0.45f, 0.70f, 0.30f };
    const float panR[4] = { 0.45f, 0.55f, 0.30f, 0.70f };
    for (int i = 0; i < 4; ++i) {
        toSink(b, constantNode(b, panL[i]), "sink.panL", { static_cast<float>(i) });
        toSink(b, constantNode(b, panR[i]), "sink.panR", { static_cast<float>(i) });
    }

    // Same spin-count-driven sine oscillator as plain Azimut/Azimut Reverb.
    NodeId spinPhase = scale(b, continuousSpinCount, 1.5f);
    NodeId sineVal = b.add("math.sine");
    b.wire(spinPhase, sineVal);
    NodeId targetLpf = b.add("math.mapRange", { -1.0f, 1.0f, 400.0f, 20000.0f });
    b.wire(sineVal, targetLpf);
    toSink(b, onePole(b, targetLpf, 0.03f), "sink.lpfCutoffHz");

    return graph;
}

} // namespace Graph::Presets
