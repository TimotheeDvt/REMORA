#include "AllPresets.h"
#include "AzimutCore.h"
#include "PresetHelpers.h"

// The 3 Azimut variants share every field via buildAzimutCore() and differ only in LPF-cutoff drive and reverb.
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
    for (int i = 0; i < 6; ++i)
        toSink(b, core.partialAmp[i], "sink.partialAmp", { static_cast<float>(i) });
    toSink(b, core.driveAmt, "sink.driveAmt");
    toSink(b, core.noiseAmount, "sink.noiseAmount");
    toSink(b, core.noiseLpCoef, "sink.noiseLpCoef");
    for (int i = 0; i < 4; ++i) {
        toSink(b, core.panL[i], "sink.panL", { static_cast<float>(i) });
        toSink(b, core.panR[i], "sink.panR", { static_cast<float>(i) });
    }
    // vibrato/tremolo rate 0 matches the new MappingOutput default - omitted.
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
    NodeId target = b.add("math.mapRange", { 0.0f, 1.0f, 400.0f, 20000.0f });
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
    toSink(b, clampNode(b, mulNodes(b, core.flowFree, wetInner), 0.0f, 1.0f), "sink.reverbWetLevel");
    NodeId roomSize = b.add("math.mapRange", { 0.0f, 1.0f, 0.25f, 0.95f });
    b.wire(core.flowFree, roomSize);
    toSink(b, roomSize, "sink.reverbRoomSize");
    NodeId damping = b.add("math.mapRange", { 0.0f, 1.0f, 0.80f, 0.15f });
    b.wire(core.flowFree, damping);
    toSink(b, damping, "sink.reverbDamping");
    return graph;
}

} // namespace Graph::Presets
