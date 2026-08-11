#include "AllPresets.h"
#include "PresetHelpers.h"

namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildSpinVoice() {
    constexpr float kPi = 3.14159265f;
    constexpr float kRootFrequencyHz = 130.81f; // C3
    static const float kVoiceBaseSemitones[4] = { 0.f, 7.f, 12.f, 19.f };

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    NodeId roll = b.add("source.roll");
    NodeId activeVoiceIndex = addConst(b, scale(b, roll, 2.0f / kPi), 1.5f);

    NodeId activeVoiceRounded = b.add("math.quantizeSteps", { 1.0f });
    b.wire(activeVoiceIndex, activeVoiceRounded);
    NodeId voiceDisplay = b.add("display.value");
    b.wire(activeVoiceRounded, voiceDisplay);
    b.setLabel(voiceDisplay, "Selected Voice");

    NodeId pitch = b.add("source.pitch");
    NodeId pitchOffset = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, -12.0f, 12.0f });
    b.wire(pitch, pitchOffset);

    NodeId yaw = b.add("source.yaw");
    NodeId yawGain = b.add("math.mapRange", { -kPi, kPi, 0.0f, 1.0f });
    b.wire(yaw, yawGain);
    NodeId targetGain = clampNode(b, yawGain, 0.0f, 1.0f);

    NodeId selectedGain = constantNode(b, 0.0f);
    NodeId selectedHz = constantNode(b, 0.0f);

    for (int v = 0; v < 4; ++v) {
        NodeId voiceGate = b.add("math.equals", { 0.5f });
        b.wire(activeVoiceIndex, voiceGate, 0);
        b.wire(constantNode(b, static_cast<float>(v)), voiceGate, 1);

        NodeId pitchTarget = addConst(b, pitchOffset, kVoiceBaseSemitones[v]);
        // Starts at 0 semitones, not kVoiceBaseSemitones[v] - a minor one-time glide-up transient, not a persistent bug.
        NodeId pitchNode = b.add("math.latchedSmoother", { 0.08f });
        b.wire(pitchTarget, pitchNode, 0);
        b.wire(voiceGate, pitchNode, 1);

        NodeId gainNode = b.add("math.latchedSmoother", { 0.15f });
        b.wire(targetGain, gainNode, 0);
        b.wire(voiceGate, gainNode, 1);

        NodeId voiceHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
        b.wire(pitchNode, voiceHz);
        toSink(b, voiceHz, "sink.voiceHz", { static_cast<float>(v) });
        toSink(b, gainNode, "sink.voiceGain", { static_cast<float>(v) });

        selectedGain = addNodes(b, selectedGain, mulNodes(b, voiceGate, gainNode));
        selectedHz = addNodes(b, selectedHz, mulNodes(b, voiceGate, voiceHz));
    }

    NodeId gainDisplay = b.add("display.value");
    b.wire(selectedGain, gainDisplay);
    b.setLabel(gainDisplay, "Voice Gain");

    NodeId hzDisplay = b.add("display.value");
    b.wire(selectedHz, hzDisplay);
    b.setLabel(hzDisplay, "Voice Hz");

    toSink(b, constantNode(b, 1.0f), "sink.useIndependentVoicePitch");
    toSink(b, constantNode(b, 4.0f), "sink.numVoices");
    toSink(b, constantNode(b, kRootFrequencyHz), "sink.rootHz");
    toSink(b, constantNode(b, 7.0f), "sink.chordSemitone", { 0.0f });
    toSink(b, constantNode(b, 12.0f), "sink.chordSemitone", { 1.0f });
    toSink(b, constantNode(b, 19.0f), "sink.chordSemitone", { 2.0f });
    toSink(b, constantNode(b, 1.0f), "sink.masterGain");

    // partialAmp[0]=1/vibratoRate=5/tremoloRate=4/noiseLpCoef=0.5 all match the new
    // MappingOutput defaults - omitted.
    const float partials[5] = { 0.5f, 0.25f, 0.1f, 0.05f, 0.02f };
    for (int i = 0; i < 5; ++i)
        toSink(b, constantNode(b, partials[i]), "sink.partialAmp", { static_cast<float>(i + 1) });

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
