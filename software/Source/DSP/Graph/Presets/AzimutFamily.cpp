#include "AllPresets.h"
#include "AzimutCore.h"
#include "PresetHelpers.h"

namespace Graph::Presets {

namespace {

void wireCoreToSinks(GraphBuilder& b, const AzimutCoreOutputs& core) {
    toSink(b, core.rootHz, "sink.rootHz");
    toSink(b, core.chordSemitone[0], "sink.chordSemitone", { 0.0f });
    toSink(b, core.chordSemitone[1], "sink.chordSemitone", { 1.0f });
    toSink(b, core.chordSemitone[2], "sink.chordSemitone", { 2.0f });
    toSink(b, core.numVoices, "sink.numVoices");
    for (int i = 0; i < 4; ++i)
        toSink(b, core.voiceGain[i], "sink.voiceGain", { static_cast<float>(i) });
    toSink(b, core.masterGain, "sink.masterGain");
    // partialAmp[0]=1 matches the new MappingOutput default - omitted.
    for (int i = 1; i < 6; ++i)
        toSink(b, core.partialAmp[i], "sink.partialAmp", { static_cast<float>(i) });
    toSink(b, core.driveAmt, "sink.driveAmt");
    toSink(b, core.noiseAmount, "sink.noiseAmount");
    toSink(b, core.noiseLpCoef, "sink.noiseLpCoef");
    for (int i = 0; i < 4; ++i) {
        toSink(b, core.panL[i], "sink.panL", { static_cast<float>(i) });
        toSink(b, core.panR[i], "sink.panR", { static_cast<float>(i) });
    }
    // No vibrato/tremolo for Azimut - unwired inputs default to 0, overriding the new nonzero default.
    b.add("sink.vibratoRateHz");
    b.add("sink.tremoloRateHz");
}

} // namespace

std::unique_ptr<NodeGraph> buildAzimut() {
    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    AzimutCoreOutputs core = buildAzimutCore(b);
    wireCoreToSinks(b, core);
    toSink(b, buildSpinCountLpfHz(b, core), "sink.lpfCutoffHz");
    return graph;
}

std::unique_ptr<NodeGraph> buildAzimutPlus() {
    constexpr float kGyroscopeFloor = 30.0f, kGyroscopeCeiling = 750.0f; // StaffMotionAnalyzer constants

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    AzimutCoreOutputs core = buildAzimutCore(b);
    wireCoreToSinks(b, core);

    // Cutoff tracks rotation speed directly instead of continuous spin count.
    NodeId speedNormRaw = b.add("math.mapRange", { kGyroscopeFloor, kGyroscopeCeiling, 0.0f, 1.0f });
    b.wire(core.gyroMagnitude, speedNormRaw);
    NodeId speedNorm = clampNode(b, speedNormRaw, 0.0f, 1.0f);
    NodeId target = b.add("math.mapRangeLog", { 0.0f, 1.0f, 400.0f, 20000.0f });
    b.wire(speedNorm, target);
    toSink(b, onePole(b, target, 0.03f), "sink.lpfCutoffHz");
    return graph;
}

std::unique_ptr<NodeGraph> buildAzimutReverb() {
    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    AzimutCoreOutputs core = buildAzimutCore(b);
    wireCoreToSinks(b, core);
    toSink(b, buildSpinCountLpfHz(b, core), "sink.lpfCutoffHz");

    // Free motion opens a longer, brighter reverb tail; bound motion collapses it back toward dry.
    NodeId wetInner = addConst(b, scale(b, core.labanWeight, 0.75f), 0.25f);
    toSink(b, scale(b, mulNodes(b, core.flowFree, wetInner), 0.2f), "sink.reverbWetLevel");
    NodeId roomSize = b.add("math.mapRange", { 0.0f, 1.0f, 0.25f, 0.95f });
    b.wire(core.flowFree, roomSize);
    toSink(b, roomSize, "sink.reverbRoomSize");
    NodeId damping = b.add("math.mapRange", { 0.0f, 1.0f, 0.80f, 0.15f });
    b.wire(core.flowFree, damping);
    toSink(b, damping, "sink.reverbDamping");
    return graph;
}

std::unique_ptr<NodeGraph> buildAzimutKinetic() {
    constexpr float kGyroscopeFloor = 30.0f, kGyroscopeCeiling = 750.0f; // StaffMotionAnalyzer constants

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    AzimutCoreOutputs core = buildAzimutCore(b);

    // Pitch and voice-gating: unchanged from the rest of the family.
    toSink(b, core.rootHz, "sink.rootHz");
    toSink(b, core.chordSemitone[0], "sink.chordSemitone", { 0.0f });
    toSink(b, core.chordSemitone[1], "sink.chordSemitone", { 1.0f });
    toSink(b, core.chordSemitone[2], "sink.chordSemitone", { 2.0f });
    toSink(b, core.numVoices, "sink.numVoices");
    for (int i = 0; i < 4; ++i)
        toSink(b, core.voiceGain[i], "sink.voiceGain", { static_cast<float>(i) });
    toSink(b, core.masterGain, "sink.masterGain");
    for (int i = 0; i < 4; ++i) {
        toSink(b, core.panL[i], "sink.panL", { static_cast<float>(i) });
        toSink(b, core.panR[i], "sink.panR", { static_cast<float>(i) });
    }

    // The one hub node: normalized, smoothed rotation speed. Everything timbral below reads only this.
    NodeId speedNormRaw = b.add("math.mapRange", { kGyroscopeFloor, kGyroscopeCeiling, 0.0f, 1.0f });
    b.wire(core.gyroMagnitude, speedNormRaw);
    NodeId energy = onePole(b, clampNode(b, speedNormRaw, 0.0f, 1.0f), 0.05f);

    NodeId lpf = b.add("math.mapRangeLog", { 0.0f, 1.0f, 300.0f, 18000.0f });
    b.wire(energy, lpf);
    toSink(b, onePole(b, lpf, 0.03f), "sink.lpfCutoffHz");

    // Pure sine at rest, increasingly buzzy/harmonic as motion energy climbs.
    const float partialPeak[5] = { 0.75f, 0.55f, 0.45f, 0.30f, 0.20f };
    for (int p = 0; p < 5; ++p) {
        NodeId amp = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, partialPeak[p] });
        b.wire(energy, amp);
        toSink(b, amp, "sink.partialAmp", { static_cast<float>(p + 1) });
    }

    NodeId drive = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, 3.0f });
    b.wire(energy, drive);
    toSink(b, drive, "sink.driveAmt");

    NodeId noiseAmt = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, 0.35f });
    b.wire(energy, noiseAmt);
    toSink(b, noiseAmt, "sink.noiseAmount");
    NodeId noiseColor = b.add("math.mapRange", { 0.0f, 1.0f, 0.85f, 0.15f });
    b.wire(energy, noiseColor);
    toSink(b, noiseColor, "sink.noiseLpCoef");

    NodeId vibDepth = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, 0.025f });
    b.wire(energy, vibDepth);
    toSink(b, vibDepth, "sink.vibratoDepth");
    toSink(b, constantNode(b, 5.5f), "sink.vibratoRateHz");
    NodeId tremDepth = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, 0.3f });
    b.wire(energy, tremDepth);
    toSink(b, tremDepth, "sink.tremoloDepth");
    toSink(b, constantNode(b, 4.5f), "sink.tremoloRateHz");

    NodeId reverbWet = b.add("math.mapRange", { 0.0f, 1.0f, 0.05f, 0.45f });
    b.wire(energy, reverbWet);
    toSink(b, reverbWet, "sink.reverbWetLevel");
    NodeId reverbRoom = b.add("math.mapRange", { 0.0f, 1.0f, 0.3f, 0.9f });
    b.wire(energy, reverbRoom);
    toSink(b, reverbRoom, "sink.reverbRoomSize");
    NodeId reverbDamp = b.add("math.mapRange", { 0.0f, 1.0f, 0.75f, 0.2f });
    b.wire(energy, reverbDamp);
    toSink(b, reverbDamp, "sink.reverbDamping");

    return graph;
}

} // namespace Graph::Presets
