#include "AllPresets.h"
#include "PresetHelpers.h"
#include <vector>

// Same roll-select / pitch-glide / yaw-gain voice mapping as Spin Voices, but every voice's pitch is
// quantized to the nearest note of a major scale, and all 4 voices' gain + note are always on display.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildSpinVoiceScale() {
    constexpr float kPi = 3.14159265f;
    constexpr float kRootFrequencyHz = 130.81f; // C3
    static const float kVoiceBaseSemitones[4] = { 0.f, 7.f, 12.f, 19.f };

    static const float kNearestScaleSemitone[12] = { 0.f, 0.f, 2.f, 2.f, 4.f, 5.f, 5.f, 7.f, 7.f, 9.f, 9.f, 11.f };
    static const float kNearestScaleDegree[12]   = { 0.f, 0.f, 1.f, 1.f, 2.f, 3.f, 3.f, 4.f, 4.f, 5.f, 5.f, 6.f };
    const std::vector<float> semitoneTable(kNearestScaleSemitone, kNearestScaleSemitone + 12);
    const std::vector<float> degreeTable(kNearestScaleDegree, kNearestScaleDegree + 12);

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);

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

    for (int v = 0; v < 4; ++v) {
        NodeId voiceGate = b.add("math.equals", { 0.5f });
        b.wire(activeVoiceIndex, voiceGate, 0);
        b.wire(constantNode(b, static_cast<float>(v)), voiceGate, 1);

        NodeId pitchTarget = addConst(b, pitchOffset, kVoiceBaseSemitones[v]);

        NodeId semitoneInOctave = b.add("math.mod");
        b.wire(pitchTarget, semitoneInOctave, 0);
        b.wire(constantNode(b, 12.0f), semitoneInOctave, 1);
        NodeId octave = b.add("math.floorDiv");
        b.wire(pitchTarget, octave, 0);
        b.wire(constantNode(b, 12.0f), octave, 1);

        NodeId degreeIndex = b.add("math.lookupTable", degreeTable);
        b.wire(semitoneInOctave, degreeIndex);
        NodeId degree = addConst(b, degreeIndex, 1.0f);
        NodeId degreeHeld = b.add("math.latchedSmoother", { 1.0f });
        b.wire(degree, degreeHeld, 0);
        b.wire(voiceGate, degreeHeld, 1);
        NodeId degreeDisplay = b.add("display.value");
        b.wire(degreeHeld, degreeDisplay);
        b.setLabel(degreeDisplay, "Voice " + juce::String(v + 1) + " Note (Degree)");

        NodeId nearestSemitone = b.add("math.lookupTable", semitoneTable);
        b.wire(semitoneInOctave, nearestSemitone);
        NodeId quantizedSemitones = addNodes(b, scale(b, octave, 12.0f), nearestSemitone);

        NodeId pitchNode = b.add("math.latchedSmoother", { 0.08f });
        b.wire(quantizedSemitones, pitchNode, 0);
        b.wire(voiceGate, pitchNode, 1);

        NodeId gainNode = b.add("math.latchedSmoother", { 0.15f });
        b.wire(targetGain, gainNode, 0);
        b.wire(voiceGate, gainNode, 1);
        NodeId gainDisplay = b.add("display.value");
        b.wire(gainNode, gainDisplay);
        b.setLabel(gainDisplay, "Voice " + juce::String(v + 1) + " Gain");

        NodeId voiceHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
        b.wire(pitchNode, voiceHz);
        b.wire(voiceHz, synth, AdditivePort::VoiceHz0 + v);
        b.wire(gainNode, synth, AdditivePort::VoiceGain0 + v);
    }

    b.wire(constantNode(b, 1.0f), synth, AdditivePort::UseIndependentVoicePitch);
    b.wire(constantNode(b, 4.0f), synth, AdditivePort::NumVoices);
    b.wire(constantNode(b, kRootFrequencyHz), synth, AdditivePort::RootHz);
    b.wire(constantNode(b, 7.0f), synth, AdditivePort::ChordSemitone0);
    b.wire(constantNode(b, 12.0f), synth, AdditivePort::ChordSemitone1);
    b.wire(constantNode(b, 19.0f), synth, AdditivePort::ChordSemitone2);
    b.wire(constantNode(b, 1.0f), gain, 0);

    // partialAmp[0]=1/vibratoRate=5/tremoloRate=4/noiseLpCoef=0.5 all match the Additive
    // Synth's own defaults - omitted.
    const float partials[5] = { 0.5f, 0.25f, 0.1f, 0.05f, 0.02f };
    for (int i = 0; i < 5; ++i)
        b.wire(constantNode(b, partials[i]), synth, AdditivePort::PartialAmp0 + (i + 1));

    b.wire(constantNode(b, 20000.0f), synth, AdditivePort::LpfCutoffHz);

    const float panL[4] = { 0.85f, 0.55f, 0.45f, 0.15f };
    const float panR[4] = { 0.15f, 0.45f, 0.55f, 0.85f };
    for (int i = 0; i < 4; ++i) {
        b.wire(constantNode(b, panL[i]), synth, AdditivePort::PanL0 + i);
        b.wire(constantNode(b, panR[i]), synth, AdditivePort::PanR0 + i);
    }

    return graph;
}

} // namespace Graph::Presets
