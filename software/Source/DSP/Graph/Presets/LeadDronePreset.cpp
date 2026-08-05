#include "AllPresets.h"
#include "PresetHelpers.h"
#include <cmath>
#include <vector>

// Reads raw StaffSoundParams directly, bypassing StaffMotionAnalyzer.
namespace Graph::Presets {

namespace {

// scaleStep -> semitones via octave*12 + majorScale[degree], baked into a table at build time since it's pure data.
std::vector<float> buildScaleTable(const int* scaleDegrees, int scaleLength, int minStep, int maxStep) {
    std::vector<float> table;
    for (int step = minStep; step <= maxStep; ++step) {
        int octave = step / scaleLength;
        int degree = step % scaleLength;
        if (degree < 0) {
            degree += scaleLength;
            octave -= 1;
        }
        table.push_back(static_cast<float>(octave * 12 + scaleDegrees[degree]));
    }
    return table;
}

} // namespace

std::unique_ptr<NodeGraph> buildLeadDrone() {
    constexpr float kPi = 3.14159265f;
    constexpr int kMinStep = -24, kMaxStep = 24; // generous margin over the realistic pitch range
    static const int kMajorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    // scaleStep = round(pitch * 7); semitones = table[scaleStep - kMinStep]
    NodeId pitch = b.add("source.pitch");
    NodeId scaleIndex = addConst(b, scale(b, pitch, 7.0f), static_cast<float>(-kMinStep));
    NodeId semitones = b.add("math.lookupTable", buildScaleTable(kMajorScale, 7, kMinStep, kMaxStep));
    b.wire(scaleIndex, semitones);

    NodeId rootHz = b.add("math.semitonesToHz", { 220.0f });
    b.wire(semitones, rootHz);
    toSink(b, rootHz, "sink.rootHz");

    toSink(b, constantNode(b, 4.0f), "sink.numVoices");
    toSink(b, subConst(b, semitones, -12.0f), "sink.chordSemitone", { 0.0f });
    toSink(b, subConst(b, semitones, -24.0f), "sink.chordSemitone", { 1.0f });
    toSink(b, subConst(b, semitones, -19.0f), "sink.chordSemitone", { 2.0f });

    NodeId gyroMag = b.add("source.gyroMagnitudeRaw");
    NodeId accelMag = b.add("source.accelMagnitudeRaw");
    NodeId yaw = b.add("source.yaw");

    // yaw is in radians ([-pi,pi]) like pitch/roll; normalize to [0,1].
    NodeId yawNorm = clampNode(b, scale(b, addConst(b, yaw, kPi), 1.0f / (2.0f * kPi)), 0.0f, 1.0f);

    toSink(b, addConst(b, clampNode(b, scale(b, gyroMag, 0.01f), 0.0f, 0.2f), 0.8f), "sink.masterGain");

    toSink(b, addConst(b, scale(b, clampNode(b, scale(b, accelMag, 0.1f), 0.0f, 1.0f), 0.4f), 0.6f), "sink.voiceGain", { 0.0f });

    NodeId droneSwell = addConst(b, scale(b, clampNode(b, scale(b, accelMag, 0.15f), 0.0f, 1.0f), 0.4f), 1.2f);
    toSink(b, mulNodes(b, droneSwell, subNodes(b, constantNode(b, 1.0f), yawNorm)), "sink.voiceGain", { 1.0f });
    toSink(b, mulNodes(b, droneSwell, yawNorm), "sink.voiceGain", { 2.0f });
    toSink(b, scale(b, droneSwell, 0.8f), "sink.voiceGain", { 3.0f });

    toSink(b, clampNode(b, scale(b, accelMag, 0.2f), 0.0f, 2.5f), "sink.driveAmt");
    toSink(b, clampNode(b, scale(b, accelMag, 0.05f), 0.0f, 0.3f), "sink.noiseAmount");
    toSink(b, constantNode(b, 0.2f), "sink.noiseLpCoef");

    toSink(b, clampNode(b, scale(b, absNode(b, pitch), 0.0005f), 0.0f, 0.02f), "sink.vibratoDepth");
    toSink(b, addConst(b, scale(b, gyroMag, 0.05f), 4.0f), "sink.vibratoRateHz");

    toSink(b, clampNode(b, scale(b, absNode(b, yaw), 0.002f), 0.0f, 0.3f), "sink.tremoloDepth");
    toSink(b, addConst(b, scale(b, accelMag, 0.1f), 2.0f), "sink.tremoloRateHz");

    NodeId brightness = clampNode(b, scale(b, accelMag, 0.15f), 0.0f, 1.0f);
    toSink(b, constantNode(b, 1.0f), "sink.partialAmp", { 0.0f });
    toSink(b, scale(b, brightness, 0.7f), "sink.partialAmp", { 1.0f });
    toSink(b, scale(b, brightness, 0.5f), "sink.partialAmp", { 2.0f });
    toSink(b, scale(b, brightness, 0.3f), "sink.partialAmp", { 3.0f });
    toSink(b, scale(b, brightness, 0.2f), "sink.partialAmp", { 4.0f });
    toSink(b, scale(b, brightness, 0.1f), "sink.partialAmp", { 5.0f });

    const float panL[4] = { 0.6f, 0.9f, 0.1f, 0.5f };
    const float panR[4] = { 0.6f, 0.1f, 0.9f, 0.5f };
    for (int i = 0; i < 4; ++i) {
        toSink(b, constantNode(b, panL[i]), "sink.panL", { static_cast<float>(i) });
        toSink(b, constantNode(b, panR[i]), "sink.panR", { static_cast<float>(i) });
    }

    return graph;
}

} // namespace Graph::Presets
