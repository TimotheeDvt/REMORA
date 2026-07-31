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

    // rootHz: midiNote = round(36 + clamp((pitch+pi/2)/pi,0,1) * 24); Hz = semitonesToHz(midiNote - 69, 440)
    NodeId pitch = b.add("source.pitch");
    NodeId pitchClamped = clampNode(b, pitch, -kPi * 0.5f, kPi * 0.5f);
    NodeId midiFloat = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, 36.0f, 60.0f });
    b.wire(pitchClamped, midiFloat);
    NodeId midiNote = b.add("math.quantizeSteps", { 1.0f });
    b.wire(midiFloat, midiNote);
    NodeId rootHz = b.add("math.semitonesToHz", { 440.0f });
    b.wire(addConst(b, midiNote, -69.0f), rootHz);
    toSink(b, rootHz, "sink.rootHz");

    // rollAbs = clamp(|roll|/pi, 0, 1)
    NodeId roll = b.add("source.roll");
    NodeId rollAbs = clampNode(b, scale(b, absNode(b, roll), 1.0f / kPi), 0.0f, 1.0f);

    // yawToChordIdx: 4-band staircase from yawNorm, or a 2-way high-roll override
    NodeId yaw = b.add("source.yaw");
    NodeId yawNorm = clampNode(b, scale(b, addConst(b, yaw, kPi), 1.0f / (2.0f * kPi)), 0.0f, 1.0f);

    auto thresholdOf = [&](NodeId src, float t) {
        NodeId n = b.add("math.threshold", { t });
        b.wire(src, n);
        return n;
    };
    NodeId band0to3 = addNodes(b, addNodes(b, thresholdOf(yawNorm, 0.25f), thresholdOf(yawNorm, 0.5f)), thresholdOf(yawNorm, 0.75f));
    NodeId highRollPair = addConst(b, thresholdOf(yawNorm, 0.5f), 4.0f); // 4 or 5
    NodeId chordIdx = b.add("math.crossfade");
    b.wire(band0to3, chordIdx, 0);
    b.wire(highRollPair, chordIdx, 1);
    b.wire(thresholdOf(rollAbs, 0.7f), chordIdx, 2);

    toSink(b, [&] { NodeId n = b.add("math.lookupTable", std::vector<float>(kChordCol0, kChordCol0 + 6)); b.wire(chordIdx, n); return n; }(), "sink.chordSemitone", { 0.0f });
    toSink(b, [&] { NodeId n = b.add("math.lookupTable", std::vector<float>(kChordCol1, kChordCol1 + 6)); b.wire(chordIdx, n); return n; }(), "sink.chordSemitone", { 1.0f });
    toSink(b, [&] { NodeId n = b.add("math.lookupTable", std::vector<float>(kChordCol2, kChordCol2 + 6)); b.wire(chordIdx, n); return n; }(), "sink.chordSemitone", { 2.0f });
    toSink(b, constantNode(b, 4.0f), "sink.numVoices");

    // bow = clamp(mapRange(gyroMag, 12, 150, 0, 1), 0, 1) - below-threshold goes negative, clamped to 0 for free.
    NodeId gyroMag = b.add("source.gyroMagnitudeRaw");
    NodeId bowRaw = b.add("math.mapRange", { 12.0f, 150.0f, 0.0f, 1.0f });
    b.wire(gyroMag, bowRaw);
    NodeId bow = clampNode(b, bowRaw, 0.0f, 1.0f);

    toSink(b, addConst(b, scale(b, bow, 0.20f), 0.05f), "sink.masterGain");
    toSink(b, scale(b, bow, 1.8f), "sink.driveAmt");
    toSink(b, scale(b, bow, 0.90f), "sink.voiceGain", { 0.0f });
    toSink(b, scale(b, bow, 0.70f), "sink.voiceGain", { 1.0f });
    toSink(b, scale(b, bow, 0.70f), "sink.voiceGain", { 2.0f });
    toSink(b, scale(b, bow, 0.90f), "sink.voiceGain", { 3.0f });

    NodeId accelMag = b.add("source.accelMagnitudeRaw");

    toSink(b, bow, "sink.partialAmp", { 0.0f });
    toSink(b, mulNodes(b, bow, addConst(b, scale(b, rollAbs, 0.4f), 0.3f)), "sink.partialAmp", { 1.0f });
    toSink(b, scale(b, bow, 0.35f), "sink.partialAmp", { 2.0f });
    toSink(b, mulNodes(b, bow, clampNode(b, scale(b, accelMag, 0.15f), 0.0f, 0.5f)), "sink.partialAmp", { 3.0f });

    // noiseEnvelope charges only on an upward strike - a frame derivative of accel magnitude.
    NodeId accelDelta = b.add("math.derivative");
    b.wire(accelMag, accelDelta);
    NodeId excess = clampNode(b, subNodes(b, accelDelta, constantNode(b, 3.0f)), 0.0f, 1000.0f);
    NodeId noiseEnvelope = b.add("math.leakyIntegrator", { 0.9990f });
    b.wire(scale(b, excess, 0.5f), noiseEnvelope);

    NodeId strikeBoost = clampNode(b, scale(b, noiseEnvelope, 1.2f), 0.0f, 0.6f);
    toSink(b, scale(b, strikeBoost, 0.4f), "sink.partialAmp", { 4.0f });
    toSink(b, scale(b, strikeBoost, 0.25f), "sink.partialAmp", { 5.0f });
    toSink(b, noiseEnvelope, "sink.noiseAmount");

    NodeId gz = b.add("source.gyroZ");
    NodeId gzAbs = absNode(b, gz);
    toSink(b, scale(b, rollAbs, 0.022f), "sink.vibratoDepth");
    toSink(b, addConst(b, scale(b, gzAbs, 0.03f), 4.5f), "sink.vibratoRateHz");
    toSink(b, clampNode(b, scale(b, gzAbs, 1.0f / 90.0f), 0.0f, 0.35f), "sink.tremoloDepth");
    toSink(b, addConst(b, scale(b, gzAbs, 0.05f), 3.0f), "sink.tremoloRateHz");

    NodeId az = b.add("source.accelZ");
    NodeId azAbsClamped = clampNode(b, scale(b, absNode(b, az), 0.18f), 0.05f, 0.90f);
    toSink(b, subConst(b, azAbsClamped, 1.0f), "sink.noiseLpCoef");

    const float panL[4] = { 0.85f, 0.55f, 0.45f, 0.15f };
    const float panR[4] = { 0.15f, 0.45f, 0.55f, 0.85f };
    for (int i = 0; i < 4; ++i) {
        toSink(b, constantNode(b, panL[i]), "sink.panL", { static_cast<float>(i) });
        toSink(b, constantNode(b, panR[i]), "sink.panR", { static_cast<float>(i) });
    }

    return graph;
}

} // namespace Graph::Presets
