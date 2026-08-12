#include "AllPresets.h"
#include "PresetHelpers.h"
#include <vector>

// Martial Effort and Martial Momentum differ too much (pitch/chord/pan logic) to share a "core" like the Azimut family does
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

std::unique_ptr<NodeGraph> buildMartialEffort() {
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
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);

    NodeId spinClass = b.add("source.spinClassification", { 1.0f }); // ByReferenceAzimuth
    NodeId isCCW = threshold(b, spinClass, 1, 0.0f); // spinDirection
    NodeId zero = constantNode(b, 0.0f);

    NodeId gyroMag = b.add("source.gyroMagnitude");

    // Schmitt-trigger step selector: only moves once it drifts more than 0.6 steps from the held step.
    NodeId gyroClamped = clampNode(b, gyroMag, kGyroscopeFloor, kGyroscopeCeiling);
    NodeId candidateStep = b.add("math.mapRange", { kGyroscopeFloor, kGyroscopeCeiling, 0.0f, 9.0f });
    b.wire(gyroClamped, candidateStep);
    NodeId currentStep = b.add("math.hysteresisStep", { 0.6f });
    b.wire(candidateStep, currentStep);
    NodeId baseSemitones = b.add("math.lookupTable", std::vector<float>(kPentatonicMinorScale, kPentatonicMinorScale + 10));
    b.wire(currentStep, baseSemitones);

    NodeId planeOffset = scale(b, spinClass, 12.0f); // isVertical (port 0, the default)
    NodeId directionOffset = scale(b, subNodes(b, constantNode(b, 1.0f), isCCW), -7.0f);
    NodeId semitones = addNodes(b, addNodes(b, baseSemitones, planeOffset), directionOffset);
    NodeId rootHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
    b.wire(semitones, rootHz);
    b.wire(rootHz, synth, AdditivePort::RootHz);

    b.wire(lut3Node(b, spinClass, isCCW, zero, std::vector<float>(kChordCol0, kChordCol0 + 8)), synth, AdditivePort::ChordSemitone0);
    b.wire(lut3Node(b, spinClass, isCCW, zero, std::vector<float>(kChordCol1, kChordCol1 + 8)), synth, AdditivePort::ChordSemitone1);
    b.wire(lut3Node(b, spinClass, isCCW, zero, std::vector<float>(kChordCol2, kChordCol2 + 8)), synth, AdditivePort::ChordSemitone2);

    NodeId labanWeight = b.add("source.labanWeight");
    NodeId isMoving = b.add("source.isMoving");
    NodeId thrustPeak = b.add("source.thrustPeakEnvelope");

    NodeId voiceLadder = addConst(b, addNodes(b, threshold(b, labanWeight, 0.25f),
        addNodes(b, threshold(b, labanWeight, 0.55f), threshold(b, labanWeight, 0.80f))), 1.0f);
    b.wire(voiceLadder, synth, AdditivePort::NumVoices);

    NodeId melodyGain = mulNodes(b, isMoving, clampNode(b, addConst(b, scale(b, gyroMag, 0.8f / kGyroscopeCeiling), 0.2f), 0.2f, 1.0f));
    b.wire(melodyGain, synth, AdditivePort::VoiceGain0);
    b.wire(scale(b, clampNode(b, scale(b, subNodes(b, labanWeight, constantNode(b, 0.25f)), 4.0f), 0.0f, 1.0f), 0.8f), synth, AdditivePort::VoiceGain1);
    b.wire(scale(b, clampNode(b, scale(b, subNodes(b, labanWeight, constantNode(b, 0.55f)), 3.3f), 0.0f, 1.0f), 0.7f), synth, AdditivePort::VoiceGain2);
    b.wire(scale(b, clampNode(b, scale(b, subNodes(b, labanWeight, constantNode(b, 0.80f)), 5.0f), 0.0f, 1.0f), 0.6f), synth, AdditivePort::VoiceGain3);

    NodeId motionGate = standardMotionGate(b, gyroMag, kGyroscopeFloor);
    b.wire(standardMasterGain(b, motionGate, labanWeight, thrustPeak), gain, 0);

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
        b.wire(value, synth, AdditivePort::PartialAmp0 + i);
    }

    NodeId driveInner = subConst(b, scale(b, spaceFocus, 0.7f), 1.0f);
    NodeId driveBase = scale(b, mulNodes(b, labanWeight, driveInner), 2.5f);
    b.wire(clampNode(b, addNodes(b, driveBase, scale(b, thrustPeak, 2.0f)), 0.0f, 4.0f), synth, AdditivePort::DriveAmt);

    NodeId suddenness = b.add("source.labanTimeSuddenness");
    NodeId noiseEnv = standardNoiseEnvelope(b, suddenness, thrustPeak, 0.9985f);
    b.wire(scale(b, noiseEnv, 0.4f), synth, AdditivePort::NoiseAmount);
    b.wire(subConst(b, addNodes(b, addConst(b, scale(b, suddenness, 0.4f), 0.2f), scale(b, thrustPeak, 0.4f)), 1.0f), synth, AdditivePort::NoiseLpCoef);

    NodeId flowBound = b.add("source.labanFlowBound");
    NodeId flowFree = b.add("source.labanFlowFree");
    b.wire(scale(b, mulNodes(b, flowFree, labanWeight), 0.020f), synth, AdditivePort::VibratoDepth);
    b.wire(addConst(b, scale(b, gyroMag, 0.005f), 4.5f), synth, AdditivePort::VibratoRateHz);
    b.wire(scale(b, mulNodes(b, flowBound, labanWeight), 0.30f), synth, AdditivePort::TremoloDepth);
    b.wire(addConst(b, scale(b, flowBound, 4.0f), 3.0f), synth, AdditivePort::TremoloRateHz);

    NodeId axisX = b.add("source.rotationAxisX");
    NodeId axisY = b.add("source.rotationAxisY");
    NodeId magSqGate = threshold(b, addNodes(b, mulNodes(b, axisX, axisX), mulNodes(b, axisY, axisY)), 1e-6f);
    NodeId azimuth = b.add("math.atan2");
    b.wire(axisY, azimuth, 0);
    b.wire(axisX, azimuth, 1);
    NodeId panBias = mulNodes(b, scale(b, azimuth, 0.2f / kPi), magSqGate);
    NodeId negPanBias = scale(b, panBias, -1.0f);

    NodeId spread = addConst(b, scale(b, flowFree, 0.5f), 0.3f);
    const float coeffL[4] = { 0.10f, -0.10f, 0.40f, -0.40f };
    for (int i = 0; i < 4; ++i) {
        b.wire(clampNode(b, addNodes(b, addConst(b, scale(b, spread, coeffL[i]), 0.5f), panBias), 0.0f, 1.0f), synth, AdditivePort::PanL0 + i);
        b.wire(clampNode(b, addNodes(b, addConst(b, scale(b, spread, -coeffL[i]), 0.5f), negPanBias), 0.0f, 1.0f), synth, AdditivePort::PanR0 + i);
    }

    return graph;
}

std::unique_ptr<NodeGraph> buildMartialMomentum() {
    constexpr float kRootFrequencyHz = 130.81f; // C3

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);

    NodeId spinClass = b.add("source.spinClassification", { 1.0f }); // ByReferenceAzimuth
    NodeId isCCW = threshold(b, spinClass, 1, 0.0f); // spinDirection

    // idx0=horiz+CW=G, idx2=horiz+CCW=A, idx4=vert+CW=C, idx6=vert+CCW=E - matches the original if/else.
    NodeId targetSemitones = lut3Node(b, spinClass, isCCW, constantNode(b, 0.0f),
        { 7.0f, 0.0f, 9.0f, 0.0f, 0.0f, 0.0f, 4.0f, 0.0f });

    NodeId gyroMag = b.add("source.gyroMagnitude");
    NodeId morphSpeed = clampNode(b, scale(b, gyroMag, 1.0f / 2000.0f), 0.005f, 0.2f);
    NodeId currentSemitones = onePoleVariableRate(b, targetSemitones, morphSpeed);
    NodeId rootHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
    b.wire(currentSemitones, rootHz);
    b.wire(rootHz, synth, AdditivePort::RootHz);

    b.wire(constantNode(b, 12.0f), synth, AdditivePort::ChordSemitone0);
    b.wire(constantNode(b, 7.0f), synth, AdditivePort::ChordSemitone1);
    b.wire(constantNode(b, -12.0f), synth, AdditivePort::ChordSemitone2);
    b.wire(constantNode(b, 4.0f), synth, AdditivePort::NumVoices);

    NodeId isMoving = b.add("source.isMoving");
    b.wire(isMoving, synth, AdditivePort::VoiceGain0);
    b.wire(scale(b, isMoving, 0.8f), synth, AdditivePort::VoiceGain1);
    b.wire(scale(b, isMoving, 0.7f), synth, AdditivePort::VoiceGain2);
    b.wire(scale(b, isMoving, 0.6f), synth, AdditivePort::VoiceGain3);

    constexpr float kGyroscopeFloor = 30.0f;
    NodeId labanWeight = b.add("source.labanWeight");
    NodeId thrustPeak = b.add("source.thrustPeakEnvelope");
    NodeId motionGate = standardMotionGate(b, gyroMag, kGyroscopeFloor);
    b.wire(standardMasterGain(b, motionGate, labanWeight, thrustPeak), gain, 0);

    NodeId peakBrightness = scale(b, thrustPeak, 0.8f);
    // partialAmp[0]=1 matches the Additive Synth's own default - omitted.
    static const float kConstantGain[6] = { 1.0f, 0.60f, 0.40f, 0.30f, 0.20f, 0.15f };
    for (int i = 1; i < 6; ++i) {
        NodeId value = constantNode(b, kConstantGain[i]);
        if (i >= 3) {
            const float peakCoeff = (i == 3) ? 0.5f : (i == 4) ? 0.7f : 0.9f;
            value = clampNode(b, addConst(b, scale(b, peakBrightness, peakCoeff), kConstantGain[i]), 0.0f, 1.0f);
        }
        b.wire(value, synth, AdditivePort::PartialAmp0 + i);
    }
    NodeId driveBase = addConst(b, labanWeight, 1.0f);
    b.wire(clampNode(b, addNodes(b, driveBase, scale(b, thrustPeak, 2.0f)), 0.0f, 4.0f), synth, AdditivePort::DriveAmt);

    NodeId suddenness = b.add("source.labanTimeSuddenness");
    NodeId noiseEnv = standardNoiseEnvelope(b, suddenness, thrustPeak, 0.9985f);
    b.wire(scale(b, noiseEnv, 0.4f), synth, AdditivePort::NoiseAmount);
    b.wire(subConst(b, addNodes(b, addConst(b, scale(b, suddenness, 0.4f), 0.2f), scale(b, thrustPeak, 0.4f)), 1.0f), synth, AdditivePort::NoiseLpCoef);

    // No vibrato/tremolo for this preset - explicitly zeroed, overriding the Additive Synth's own nonzero default.
    b.wire(constantNode(b, 0.0f), synth, AdditivePort::VibratoRateHz);
    b.wire(constantNode(b, 0.0f), synth, AdditivePort::TremoloRateHz);

    const float panL[4] = { 0.55f, 0.45f, 0.70f, 0.30f };
    const float panR[4] = { 0.45f, 0.55f, 0.30f, 0.70f };
    for (int i = 0; i < 4; ++i) {
        b.wire(constantNode(b, panL[i]), synth, AdditivePort::PanL0 + i);
        b.wire(constantNode(b, panR[i]), synth, AdditivePort::PanR0 + i);
    }

    // Same spin-count-driven sine oscillator as plain Azimut/Azimut Reverb.
    NodeId spinPhase = scale(b, spinClass, 2, 1.5f); // continuous spin count
    NodeId sineVal = b.add("math.sine");
    b.wire(spinPhase, sineVal);
    NodeId targetLpf = b.add("math.mapRangeLog", { -1.0f, 1.0f, 400.0f, 20000.0f });
    b.wire(sineVal, targetLpf);
    b.wire(onePole(b, targetLpf, 0.03f), synth, AdditivePort::LpfCutoffHz);

    return graph;
}

} // namespace Graph::Presets
