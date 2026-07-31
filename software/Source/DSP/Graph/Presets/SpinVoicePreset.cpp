#include "AllPresets.h"
#include "PresetHelpers.h"

// Spin plane+direction selects 1 of 4 voices; the other 3 freeze in place.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildSpinVoice() {
    constexpr float kRootFrequencyHz = 130.81f; // C3
    constexpr float kGyroscopeFloor = 30.0f, kGyroscopeCeiling = 750.0f;
    static const float kVoiceBaseSemitones[4] = { 0.f, 7.f, 12.f, 19.f };

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    NodeId spinClass = b.add("source.spinClassification", { 0.0f }); // ByAbsoluteComponent
    NodeId isVertical = tapPort(b, spinClass, 0);
    NodeId spinDirection = tapPort(b, spinClass, 1);
    NodeId isCCW = threshold(b, spinDirection, 0.0f);

    // activeVoiceIndex = (1-isVertical)*2 + isCCW: 0=vert+CW, 1=vert+CCW, 2=horiz+CW, 3=horiz+CCW
    NodeId planeIndex = subNodes(b, constantNode(b, 1.0f), isVertical);
    NodeId activeVoiceIndex = addNodes(b, scale(b, planeIndex, 2.0f), isCCW);

    NodeId isMoving = b.add("source.isMoving");
    NodeId gyroMag = b.add("source.gyroMagnitude");
    NodeId speedNorm = clampNode(b, [&] {
        NodeId n = b.add("math.mapRange", { kGyroscopeFloor, kGyroscopeCeiling, 0.0f, 1.0f });
        b.wire(gyroMag, n);
        return n;
    }(), 0.0f, 1.0f);

    NodeId labanWeight = b.add("source.labanWeight");
    NodeId targetGain = clampNode(b, scale(b, labanWeight, 1.2f), 0.0f, 1.0f);

    for (int v = 0; v < 4; ++v) {
        NodeId voiceGate = b.add("math.equals", { 0.5f });
        b.wire(activeVoiceIndex, voiceGate, 0);
        b.wire(constantNode(b, static_cast<float>(v)), voiceGate, 1);

        NodeId pitchGate = mulNodes(b, voiceGate, isMoving);
        NodeId pitchTarget = addConst(b, scale(b, speedNorm, 12.0f), kVoiceBaseSemitones[v]);
        // Starts at 0 semitones, not kVoiceBaseSemitones[v] - a minor one-time glide-up transient, not a persistent bug.
        NodeId pitchNode = b.add("math.latchedSmoother", { 0.08f });
        b.wire(pitchTarget, pitchNode, 0);
        b.wire(pitchGate, pitchNode, 1);

        NodeId gainNode = b.add("math.latchedSmoother", { 0.15f });
        b.wire(targetGain, gainNode, 0);
        b.wire(voiceGate, gainNode, 1);

        NodeId voiceHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
        b.wire(pitchNode, voiceHz);
        toSink(b, voiceHz, "sink.voiceHz", { static_cast<float>(v) });
        toSink(b, gainNode, "sink.voiceGain", { static_cast<float>(v) });
    }

    toSink(b, constantNode(b, 1.0f), "sink.useIndependentVoicePitch");
    toSink(b, constantNode(b, 4.0f), "sink.numVoices");
    toSink(b, constantNode(b, kRootFrequencyHz), "sink.rootHz");
    toSink(b, constantNode(b, 7.0f), "sink.chordSemitone", { 0.0f });
    toSink(b, constantNode(b, 12.0f), "sink.chordSemitone", { 1.0f });
    toSink(b, constantNode(b, 19.0f), "sink.chordSemitone", { 2.0f });
    toSink(b, constantNode(b, 1.0f), "sink.masterGain");

    const float partials[6] = { 1.0f, 0.5f, 0.25f, 0.1f, 0.05f, 0.02f };
    for (int i = 0; i < 6; ++i)
        toSink(b, constantNode(b, partials[i]), "sink.partialAmp", { static_cast<float>(i) });

    toSink(b, constantNode(b, 0.0f), "sink.driveAmt");
    toSink(b, constantNode(b, 0.0f), "sink.vibratoDepth");
    toSink(b, constantNode(b, 5.0f), "sink.vibratoRateHz");
    toSink(b, constantNode(b, 0.0f), "sink.tremoloDepth");
    toSink(b, constantNode(b, 4.0f), "sink.tremoloRateHz");
    toSink(b, constantNode(b, 0.0f), "sink.noiseAmount");
    toSink(b, constantNode(b, 0.5f), "sink.noiseLpCoef");
    toSink(b, constantNode(b, 20000.0f), "sink.lpfCutoffHz");

    const float panL[4] = { 0.85f, 0.55f, 0.45f, 0.15f };
    const float panR[4] = { 0.15f, 0.45f, 0.55f, 0.85f };
    for (int i = 0; i < 4; ++i) {
        toSink(b, constantNode(b, panL[i]), "sink.panL", { static_cast<float>(i) });
        toSink(b, constantNode(b, panR[i]), "sink.panR", { static_cast<float>(i) });
    }

    return graph;
}

} // namespace Graph::Presets
