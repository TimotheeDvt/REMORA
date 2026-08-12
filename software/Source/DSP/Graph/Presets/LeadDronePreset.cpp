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
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);

    // scaleStep = round(pitch * 7); semitones = table[scaleStep - kMinStep]
    NodeId pitch = b.add("source.pitch");
    NodeId scaleIndex = addConst(b, scale(b, pitch, 7.0f), static_cast<float>(-kMinStep));
    NodeId semitones = b.add("math.lookupTable", buildScaleTable(kMajorScale, 7, kMinStep, kMaxStep));
    b.wire(scaleIndex, semitones);

    NodeId rootHz = b.add("math.semitonesToHz", { 220.0f });
    b.wire(semitones, rootHz);
    b.wire(rootHz, synth, AdditivePort::RootHz);

    b.wire(constantNode(b, 4.0f), synth, AdditivePort::NumVoices);
    b.wire(subConst(b, semitones, -12.0f), synth, AdditivePort::ChordSemitone0);
    b.wire(subConst(b, semitones, -24.0f), synth, AdditivePort::ChordSemitone1);
    b.wire(subConst(b, semitones, -19.0f), synth, AdditivePort::ChordSemitone2);

    NodeId gyroMag = b.add("source.gyroMagnitudeRaw");
    NodeId accelMag = b.add("source.accelMagnitudeRaw");
    NodeId yaw = b.add("source.yaw");

    NodeId yawNorm = scale(b, addConst(b, yaw, kPi), 1.0f / (2.0f * kPi));

    b.wire(addConst(b, clampNode(b, scale(b, gyroMag, 0.01f), 0.0f, 0.2f), 0.8f), gain, 0);

    b.wire(addConst(b, scale(b, clampNode(b, scale(b, accelMag, 0.1f), 0.0f, 1.0f), 0.4f), 0.6f), synth, AdditivePort::VoiceGain0);

    NodeId droneSwell = addConst(b, scale(b, clampNode(b, scale(b, accelMag, 0.15f), 0.0f, 1.0f), 0.4f), 1.2f);
    b.wire(mulNodes(b, droneSwell, subNodes(b, constantNode(b, 1.0f), yawNorm)), synth, AdditivePort::VoiceGain1);
    b.wire(mulNodes(b, droneSwell, yawNorm), synth, AdditivePort::VoiceGain2);
    b.wire(scale(b, droneSwell, 0.8f), synth, AdditivePort::VoiceGain3);

    b.wire(clampNode(b, scale(b, accelMag, 0.2f), 0.0f, 2.5f), synth, AdditivePort::DriveAmt);
    b.wire(clampNode(b, scale(b, accelMag, 0.05f), 0.0f, 0.3f), synth, AdditivePort::NoiseAmount);
    b.wire(constantNode(b, 0.2f), synth, AdditivePort::NoiseLpCoef);

    b.wire(clampNode(b, scale(b, absNode(b, pitch), 0.0005f), 0.0f, 0.02f), synth, AdditivePort::VibratoDepth);
    b.wire(addConst(b, scale(b, gyroMag, 0.05f), 4.0f), synth, AdditivePort::VibratoRateHz);

    b.wire(clampNode(b, scale(b, absNode(b, yaw), 0.002f), 0.0f, 0.3f), synth, AdditivePort::TremoloDepth);
    b.wire(addConst(b, scale(b, accelMag, 0.1f), 2.0f), synth, AdditivePort::TremoloRateHz);

    // partialAmp[0]=1 matches the Additive Synth's own default - omitted.
    NodeId brightness = clampNode(b, scale(b, accelMag, 0.15f), 0.0f, 1.0f);
    b.wire(scale(b, brightness, 0.7f), synth, AdditivePort::PartialAmp1);
    b.wire(scale(b, brightness, 0.5f), synth, AdditivePort::PartialAmp2);
    b.wire(scale(b, brightness, 0.3f), synth, AdditivePort::PartialAmp3);
    b.wire(scale(b, brightness, 0.2f), synth, AdditivePort::PartialAmp4);
    b.wire(scale(b, brightness, 0.1f), synth, AdditivePort::PartialAmp5);

    // voice 3's pan (0.5/0.5) matches the Additive Synth's own default - omitted.
    const float panL[3] = { 0.6f, 0.9f, 0.1f };
    const float panR[3] = { 0.6f, 0.1f, 0.9f };
    for (int i = 0; i < 3; ++i) {
        b.wire(constantNode(b, panL[i]), synth, AdditivePort::PanL0 + i);
        b.wire(constantNode(b, panR[i]), synth, AdditivePort::PanR0 + i);
    }

    return graph;
}

} // namespace Graph::Presets
