#include "AllPresets.h"
#include "AzimutCore.h"
#include "PresetHelpers.h"

namespace Graph::Presets {

namespace {

void wireCoreToSinks(GraphBuilder& b, NodeId synth, NodeId gain, const AzimutCoreOutputs& core, const AzimutTimbreOutputs& timbre) {
    b.wire(core.rootHz, synth, AdditivePort::RootHz);
    b.wire(core.chordSemitone[0], synth, AdditivePort::ChordSemitone0);
    b.wire(core.chordSemitone[1], synth, AdditivePort::ChordSemitone1);
    b.wire(core.chordSemitone[2], synth, AdditivePort::ChordSemitone2);
    b.wire(core.numVoices, synth, AdditivePort::NumVoices);
    for (int i = 0; i < 4; ++i)
        b.wire(core.voiceGain[i], synth, AdditivePort::VoiceGain0 + i);
    b.wire(core.masterGain, gain, 0);
    // partialAmp[0]=1 matches the Additive Synth's own default - omitted.
    for (int i = 1; i < 6; ++i)
        b.wire(timbre.partialAmp[i], synth, AdditivePort::PartialAmp0 + i);
    b.wire(timbre.driveAmt, synth, AdditivePort::DriveAmt);
    b.wire(timbre.noiseAmount, synth, AdditivePort::NoiseAmount);
    b.wire(timbre.noiseLpCoef, synth, AdditivePort::NoiseLpCoef);
    for (int i = 0; i < 4; ++i) {
        b.wire(core.panL[i], synth, AdditivePort::PanL0 + i);
        b.wire(core.panR[i], synth, AdditivePort::PanR0 + i);
    }
    // No vibrato/tremolo for Azimut - VibratoDepth/TremoloDepth are left at their own 0 default (never
    // wired below), which alone silences both regardless of rate, so there's nothing to wire here.
}

} // namespace

std::unique_ptr<NodeGraph> buildAzimut() {
    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);
    AzimutCoreOutputs core = buildAzimutCore(b);
    AzimutTimbreOutputs timbre = buildAzimutTimbre(b, core);
    wireCoreToSinks(b, synth, gain, core, timbre);
    b.wire(buildSpinCountLpfHz(b, core), synth, AdditivePort::LpfCutoffHz);
    return graph;
}

std::unique_ptr<NodeGraph> buildAzimutPlus() {
    constexpr float kGyroscopeFloor = 30.0f, kGyroscopeCeiling = 750.0f; // StaffMotionAnalyzer constants

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);
    AzimutCoreOutputs core = buildAzimutCore(b);
    AzimutTimbreOutputs timbre = buildAzimutTimbre(b, core);
    wireCoreToSinks(b, synth, gain, core, timbre);

    // Cutoff tracks rotation speed directly instead of continuous spin count.
    NodeId speedNormRaw = b.add("math.mapRange", { kGyroscopeFloor, kGyroscopeCeiling, 0.0f, 1.0f });
    b.wire(core.gyroMagnitude, speedNormRaw);
    NodeId speedNorm = clampNode(b, speedNormRaw, 0.0f, 1.0f);
    NodeId target = b.add("math.mapRangeLog", { 0.0f, 1.0f, 400.0f, 20000.0f });
    b.wire(speedNorm, target);
    b.wire(onePole(b, target, 0.03f), synth, AdditivePort::LpfCutoffHz);
    return graph;
}

std::unique_ptr<NodeGraph> buildAzimutReverb() {
    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);
    AzimutCoreOutputs core = buildAzimutCore(b);
    AzimutTimbreOutputs timbre = buildAzimutTimbre(b, core);
    wireCoreToSinks(b, synth, gain, core, timbre);
    b.wire(buildSpinCountLpfHz(b, core), synth, AdditivePort::LpfCutoffHz);

    // Free motion opens a longer, brighter reverb tail; bound motion collapses it back toward dry.
    NodeId wetInner = addConst(b, scale(b, core.labanWeight, 0.75f), 0.25f);
    b.wire(scale(b, mulNodes(b, timbre.flowFree, wetInner), 0.2f), synth, AdditivePort::ReverbWetLevel);
    NodeId roomSize = b.add("math.mapRange", { 0.0f, 1.0f, 0.25f, 0.95f });
    b.wire(timbre.flowFree, roomSize);
    b.wire(roomSize, synth, AdditivePort::ReverbRoomSize);
    NodeId damping = b.add("math.mapRange", { 0.0f, 1.0f, 0.80f, 0.15f });
    b.wire(timbre.flowFree, damping);
    b.wire(damping, synth, AdditivePort::ReverbDamping);
    return graph;
}

std::unique_ptr<NodeGraph> buildAzimutKinetic() {
    constexpr float kGyroscopeFloor = 30.0f, kGyroscopeCeiling = 750.0f; // StaffMotionAnalyzer constants

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);
    AzimutCoreOutputs core = buildAzimutCore(b);

    // Pitch and voice-gating: unchanged from the rest of the family.
    b.wire(core.rootHz, synth, AdditivePort::RootHz);
    b.wire(core.chordSemitone[0], synth, AdditivePort::ChordSemitone0);
    b.wire(core.chordSemitone[1], synth, AdditivePort::ChordSemitone1);
    b.wire(core.chordSemitone[2], synth, AdditivePort::ChordSemitone2);
    b.wire(core.numVoices, synth, AdditivePort::NumVoices);
    for (int i = 0; i < 4; ++i)
        b.wire(core.voiceGain[i], synth, AdditivePort::VoiceGain0 + i);
    b.wire(core.masterGain, gain, 0);
    for (int i = 0; i < 4; ++i) {
        b.wire(core.panL[i], synth, AdditivePort::PanL0 + i);
        b.wire(core.panR[i], synth, AdditivePort::PanR0 + i);
    }

    NodeId speedNormRaw = b.add("math.mapRange", { kGyroscopeFloor, kGyroscopeCeiling, 0.0f, 1.0f });
    b.wire(core.gyroMagnitude, speedNormRaw);
    NodeId energy = onePole(b, clampNode(b, speedNormRaw, 0.0f, 1.0f), 0.05f);

    NodeId spinCountDelta = b.add("math.derivative");
    b.wire(core.spinClassNode, 2, spinCountDelta, 0); // continuous spin count
    NodeId rotationPulse = threshold(b, spinCountDelta, 0.5f); // 1 for the block a rotation completes on
    NodeId randHold = b.add("math.sampleHoldRandom");
    b.wire(rotationPulse, randHold);

    auto driftChannel = [&](int port) {
        NodeId tap = b.add("math.passthrough");
        b.wire(randHold, port, tap, 0);
        return onePole(b, tap, 0.05f);
    };
    NodeId driftCutoff = driftChannel(0);
    NodeId driftDrive = driftChannel(1);
    NodeId driftNoise = driftChannel(2);
    NodeId driftColor = driftChannel(3);

    NodeId energyWithDrift = clampNode(b, addNodes(b, energy, scale(b, driftCutoff, 0.18f)), 0.0f, 1.0f);
    NodeId lpf = b.add("math.mapRangeLog", { 0.0f, 1.0f, 300.0f, 18000.0f });
    b.wire(energyWithDrift, lpf);
    b.wire(onePole(b, lpf, 0.03f), synth, AdditivePort::LpfCutoffHz);

    const float partialPeak[5] = { 0.75f, 0.55f, 0.45f, 0.30f, 0.20f };
    for (int p = 0; p < 5; ++p) {
        NodeId amp = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, partialPeak[p] });
        b.wire(energy, amp);
        NodeId ampDrifted = clampNode(b, addNodes(b, amp, scale(b, driftColor, partialPeak[p] * 0.4f)),
                                       0.0f, partialPeak[p] * 1.4f);
        b.wire(ampDrifted, synth, AdditivePort::PartialAmp0 + (p + 1));
    }

    NodeId drive = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, 3.0f });
    b.wire(energy, drive);
    NodeId driveDrifted = clampNode(b, addNodes(b, drive, scale(b, driftDrive, 0.6f)), 0.0f, 3.5f);
    b.wire(driveDrifted, synth, AdditivePort::DriveAmt);

    NodeId noiseAmt = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, 0.35f });
    b.wire(energy, noiseAmt);
    NodeId noiseAmtDrifted = clampNode(b, addNodes(b, noiseAmt, scale(b, driftNoise, 0.12f)), 0.0f, 0.6f);
    b.wire(noiseAmtDrifted, synth, AdditivePort::NoiseAmount);
    NodeId noiseColor = b.add("math.mapRange", { 0.0f, 1.0f, 0.85f, 0.15f });
    b.wire(energy, noiseColor);
    NodeId noiseColorDrifted = clampNode(b, addNodes(b, noiseColor, scale(b, driftNoise, 0.15f)), 0.05f, 0.95f);
    b.wire(noiseColorDrifted, synth, AdditivePort::NoiseLpCoef);

    NodeId vibDepth = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, 0.025f });
    b.wire(energy, vibDepth);
    b.wire(vibDepth, synth, AdditivePort::VibratoDepth);
    b.wire(constantNode(b, 5.5f), synth, AdditivePort::VibratoRateHz);
    NodeId tremDepth = b.add("math.mapRange", { 0.0f, 1.0f, 0.0f, 0.3f });
    b.wire(energy, tremDepth);
    b.wire(tremDepth, synth, AdditivePort::TremoloDepth);
    b.wire(constantNode(b, 4.5f), synth, AdditivePort::TremoloRateHz);

    NodeId reverbWet = b.add("math.mapRange", { 0.0f, 1.0f, 0.05f, 0.45f });
    b.wire(energy, reverbWet);
    NodeId reverbWetDrifted = clampNode(b, addNodes(b, reverbWet, scale(b, driftColor, 0.1f)), 0.0f, 0.6f);
    b.wire(reverbWetDrifted, synth, AdditivePort::ReverbWetLevel);
    NodeId reverbRoom = b.add("math.mapRange", { 0.0f, 1.0f, 0.3f, 0.9f });
    b.wire(energy, reverbRoom);
    b.wire(reverbRoom, synth, AdditivePort::ReverbRoomSize);
    NodeId reverbDamp = b.add("math.mapRange", { 0.0f, 1.0f, 0.75f, 0.2f });
    b.wire(energy, reverbDamp);
    b.wire(reverbDamp, synth, AdditivePort::ReverbDamping);

    return graph;
}

} // namespace Graph::Presets
