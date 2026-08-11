#include "AllPresets.h"
#include "PresetHelpers.h"
#include <vector>

// Same roll-select / pitch-glide / yaw-gain voice mapping as Spin Voices, but every voice's pitch is
// quantized to the nearest note of a major scale, and all 4 voices' gain + note are always on display.
namespace Graph::Presets {

namespace {

// stepIndex -> semitones via octave*12 + majorScale[degree] (same table trick as Lead Drone / Spin Filter).
std::vector<float> buildScaleSemitoneTable(const int* scaleDegrees, int scaleLength, int minStep, int maxStep) {
    std::vector<float> table;
    for (int step = minStep; step <= maxStep; ++step) {
        int octave = step / scaleLength;
        int degree = step % scaleLength;
        if (degree < 0) { degree += scaleLength; octave -= 1; }
        table.push_back(static_cast<float>(octave * 12 + scaleDegrees[degree]));
    }
    return table;
}

// stepIndex -> 1-based scale degree (1..scaleLength), folding octaves away - what the display shows
// instead of a raw Hz value.
std::vector<float> buildScaleDegreeTable(int scaleLength, int minStep, int maxStep) {
    std::vector<float> table;
    for (int step = minStep; step <= maxStep; ++step) {
        int degree = step % scaleLength;
        if (degree < 0) degree += scaleLength;
        table.push_back(static_cast<float>(degree + 1));
    }
    return table;
}

} // namespace

std::unique_ptr<NodeGraph> buildSpinVoiceScale() {
    constexpr float kPi = 3.14159265f;
    constexpr float kRootFrequencyHz = 130.81f; // C3
    constexpr int kScaleLength = 7;
    constexpr int kMinStep = -12, kMaxStep = 24; // generous margin over the realistic pitch range
    static const int kMajorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    // Same voice chord (root/5th/octave/octave+5th) as Spin Voices, expressed in scale steps rather than
    // semitones: step 4 = a 5th (7 semitones), step 7 = an octave (12 semitones), step 11 = octave+5th (19).
    static const float kVoiceBaseSteps[4] = { 0.f, 4.f, 7.f, 11.f };

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    const std::vector<float> semitoneTable = buildScaleSemitoneTable(kMajorScale, kScaleLength, kMinStep, kMaxStep);
    const std::vector<float> degreeTable = buildScaleDegreeTable(kScaleLength, kMinStep, kMaxStep);

    NodeId roll = b.add("source.roll");
    NodeId activeVoiceIndex = addConst(b, scale(b, roll, 2.0f / kPi), 1.5f);

    NodeId activeVoiceRounded = b.add("math.quantizeSteps", { 1.0f });
    b.wire(activeVoiceIndex, activeVoiceRounded);
    NodeId voiceDisplay = b.add("display.value");
    b.wire(activeVoiceRounded, voiceDisplay);
    b.setLabel(voiceDisplay, "Selected Voice");

    NodeId pitch = b.add("source.pitch");
    // +-1 octave (+-7 scale steps) across the pitch tilt range, mirroring the original's +-12 semitones.
    NodeId pitchOffsetSteps = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, -7.0f, 7.0f });
    b.wire(pitch, pitchOffsetSteps);

    NodeId yaw = b.add("source.yaw");
    NodeId yawGain = b.add("math.mapRange", { -kPi, kPi, 0.0f, 1.0f });
    b.wire(yaw, yawGain);
    NodeId targetGain = clampNode(b, yawGain, 0.0f, 1.0f);

    for (int v = 0; v < 4; ++v) {
        NodeId voiceGate = b.add("math.equals", { 0.5f });
        b.wire(activeVoiceIndex, voiceGate, 0);
        b.wire(constantNode(b, static_cast<float>(v)), voiceGate, 1);

        NodeId stepTarget = addConst(b, pitchOffsetSteps, kVoiceBaseSteps[v]);
        NodeId stepIndex = addConst(b, stepTarget, static_cast<float>(-kMinStep));

        NodeId degreeNode = b.add("math.lookupTable", degreeTable);
        b.wire(stepIndex, degreeNode);
        NodeId degreeDisplay = b.add("display.value");
        b.wire(degreeNode, degreeDisplay);
        b.setLabel(degreeDisplay, "Voice " + juce::String(v + 1) + " Note (Degree)");

        NodeId pitchTarget = b.add("math.lookupTable", semitoneTable);
        b.wire(stepIndex, pitchTarget);
        // Starts at 0 semitones, not the target - a minor one-time glide-up transient, not a persistent bug.
        NodeId pitchNode = b.add("math.latchedSmoother", { 0.08f });
        b.wire(pitchTarget, pitchNode, 0);
        b.wire(voiceGate, pitchNode, 1);

        NodeId gainNode = b.add("math.latchedSmoother", { 0.15f });
        b.wire(targetGain, gainNode, 0);
        b.wire(voiceGate, gainNode, 1);
        NodeId gainDisplay = b.add("display.value");
        b.wire(gainNode, gainDisplay);
        b.setLabel(gainDisplay, "Voice " + juce::String(v + 1) + " Gain");

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
