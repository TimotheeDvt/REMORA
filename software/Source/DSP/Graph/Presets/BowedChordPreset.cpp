#include "AllPresets.h"
#include "PresetHelpers.h"
#include <vector>

// Quantized-pitch root, yaw-banded chord, gyro-magnitude "bow pressure" gate.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildBowedChord() {
    constexpr float kPi = 3.14159265f;
    static const float kChordCol0[6] = { 3.f, 7.f, 4.f, 5.f, 3.f, 4.f };
    static const float kChordCol1[6] = { 7.f, 12.f, 7.f, 7.f, 7.f, 7.f };
    static const float kChordCol2[6] = { 12.f, 19.f, 12.f, 12.f, 10.f, 11.f };

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);

    NodeId pitch = b.add("source.pitch");
    NodeId midiFloat = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, 36.0f, 60.0f });
    b.wire(pitch, midiFloat);
    NodeId midiNote = b.add("math.quantizeSteps", { 1.0f });
    b.wire(midiFloat, midiNote);
    NodeId rootHz = b.add("math.semitonesToHz", { 440.0f });
    b.wire(addConst(b, midiNote, -69.0f), rootHz);
    b.wire(rootHz, synth, AdditivePort::RootHz);

    NodeId roll = b.add("source.roll");
    NodeId rollAbs = scale(b, absNode(b, roll), 1.0f / kPi);

    NodeId yaw = b.add("source.yaw");
    NodeId yawNorm = scale(b, addConst(b, yaw, kPi), 1.0f / (2.0f * kPi));

    auto thresholdOf = [&](NodeId src, float t) {
        NodeId n = b.add("math.threshold", { t });
        b.wire(src, n);
        return n;
    };
    NodeId yawAbove50 = thresholdOf(yawNorm, 0.5f);
    NodeId band0to3 = addNodes(b, addNodes(b, thresholdOf(yawNorm, 0.25f), yawAbove50), thresholdOf(yawNorm, 0.75f));
    NodeId highRollPair = addConst(b, yawAbove50, 4.0f); // 4 or 5
    NodeId chordIdx = b.add("math.crossfade");
    b.wire(band0to3, chordIdx, 0);
    b.wire(highRollPair, chordIdx, 1);
    b.wire(thresholdOf(rollAbs, 0.7f), chordIdx, 2);

    b.wire([&] { NodeId n = b.add("math.lookupTable", std::vector<float>(kChordCol0, kChordCol0 + 6)); b.wire(chordIdx, n); return n; }(), synth, AdditivePort::ChordSemitone0);
    b.wire([&] { NodeId n = b.add("math.lookupTable", std::vector<float>(kChordCol1, kChordCol1 + 6)); b.wire(chordIdx, n); return n; }(), synth, AdditivePort::ChordSemitone1);
    b.wire([&] { NodeId n = b.add("math.lookupTable", std::vector<float>(kChordCol2, kChordCol2 + 6)); b.wire(chordIdx, n); return n; }(), synth, AdditivePort::ChordSemitone2);
    b.wire(constantNode(b, 4.0f), synth, AdditivePort::NumVoices);

    // bow = clamp(mapRange(gyroMag, 12, 150, 0, 1), 0, 1) - below-threshold goes negative, clamped to 0 for free.
    NodeId gyroMag = b.add("source.gyroMagnitudeRaw");
    NodeId bowRaw = b.add("math.mapRange", { 12.0f, 150.0f, 0.0f, 1.0f });
    b.wire(gyroMag, bowRaw);
    NodeId bow = clampNode(b, bowRaw, 0.0f, 1.0f);

    b.wire(addConst(b, scale(b, bow, 0.20f), 0.05f), gain, 0);
    b.wire(scale(b, bow, 1.8f), synth, AdditivePort::DriveAmt);
    NodeId bowOuter = scale(b, bow, 0.90f);
    NodeId bowInner = scale(b, bow, 0.70f);
    b.wire(bowOuter, synth, AdditivePort::VoiceGain0);
    b.wire(bowInner, synth, AdditivePort::VoiceGain1);
    b.wire(bowInner, synth, AdditivePort::VoiceGain2);
    b.wire(bowOuter, synth, AdditivePort::VoiceGain3);

    NodeId accelMag = b.add("source.accelMagnitudeRaw");

    b.wire(bow, synth, AdditivePort::PartialAmp0);
    b.wire(mulNodes(b, bow, addConst(b, scale(b, rollAbs, 0.4f), 0.3f)), synth, AdditivePort::PartialAmp1);
    b.wire(scale(b, bow, 0.35f), synth, AdditivePort::PartialAmp2);
    b.wire(mulNodes(b, bow, clampNode(b, scale(b, accelMag, 0.15f), 0.0f, 0.5f)), synth, AdditivePort::PartialAmp3);

    // noiseEnvelope charges only on an upward strike - a frame derivative of accel magnitude.
    NodeId accelDelta = b.add("math.derivative");
    b.wire(accelMag, accelDelta);
    NodeId excess = clampNode(b, subNodes(b, accelDelta, constantNode(b, 3.0f)), 0.0f, 1000.0f);
    NodeId noiseEnvelope = b.add("math.leakyIntegrator", { 0.9990f });
    b.wire(scale(b, excess, 0.5f), noiseEnvelope);

    NodeId strikeBoost = clampNode(b, scale(b, noiseEnvelope, 1.2f), 0.0f, 0.6f);
    b.wire(scale(b, strikeBoost, 0.4f), synth, AdditivePort::PartialAmp4);
    b.wire(scale(b, strikeBoost, 0.25f), synth, AdditivePort::PartialAmp5);
    b.wire(noiseEnvelope, synth, AdditivePort::NoiseAmount);

    NodeId gz = b.add("source.gyroZ");
    NodeId gzAbs = absNode(b, gz);
    b.wire(scale(b, rollAbs, 0.022f), synth, AdditivePort::VibratoDepth);
    b.wire(addConst(b, scale(b, gzAbs, 0.03f), 4.5f), synth, AdditivePort::VibratoRateHz);
    b.wire(clampNode(b, scale(b, gzAbs, 1.0f / 90.0f), 0.0f, 0.35f), synth, AdditivePort::TremoloDepth);
    b.wire(addConst(b, scale(b, gzAbs, 0.05f), 3.0f), synth, AdditivePort::TremoloRateHz);

    NodeId az = b.add("source.accelZ");
    NodeId azAbsClamped = clampNode(b, scale(b, absNode(b, az), 0.18f), 0.05f, 0.90f);
    b.wire(subConst(b, azAbsClamped, 1.0f), synth, AdditivePort::NoiseLpCoef);

    const float panL[4] = { 0.85f, 0.55f, 0.45f, 0.15f };
    const float panR[4] = { 0.15f, 0.45f, 0.55f, 0.85f };
    for (int i = 0; i < 4; ++i) {
        b.wire(constantNode(b, panL[i]), synth, AdditivePort::PanL0 + i);
        b.wire(constantNode(b, panR[i]), synth, AdditivePort::PanR0 + i);
    }

    return graph;
}

} // namespace Graph::Presets
