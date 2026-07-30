#include "../GraphBuilder.h"
#include "AllPresets.h"

// Reproduces the retired SimpleMapping: one voice, root pitch from staff
// pitch angle, gain from |roll|, static center pan, no timbre/modulation.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildSimple() {
    constexpr float kPi = 3.14159265f;

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    // rootHz = 100 + clamp((pitch + pi/2)/pi, 0, 1) * 900
    NodeId pitch = b.add("source.pitch");
    NodeId pitchClamped = b.add("math.clamp", { -kPi * 0.5f, kPi * 0.5f });
    NodeId rootHz = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, 100.0f, 1000.0f });
    NodeId rootSink = b.add("sink.rootHz");
    b.wire(pitch, pitchClamped).wire(pitchClamped, rootHz).wire(rootHz, rootSink);

    // masterGain = 0.05 + clamp(|roll|/pi, 0, 1) * 0.15
    NodeId roll = b.add("source.roll");
    NodeId rollAbs = b.add("math.abs");
    NodeId gain = b.add("math.mapRange", { 0.0f, kPi, 0.05f, 0.20f });
    NodeId gainClamped = b.add("math.clamp", { 0.05f, 0.20f });
    NodeId gainSink = b.add("sink.masterGain");
    b.wire(roll, rollAbs).wire(rollAbs, gain).wire(gain, gainClamped).wire(gainClamped, gainSink);

    NodeId numVoices = b.add("math.constant", { 1.0f });
    b.wire(numVoices, b.add("sink.numVoices"));

    NodeId voiceGain0 = b.add("math.constant", { 1.0f });
    b.wire(voiceGain0, b.add("sink.voiceGain", { 0.0f }));
    for (int i = 1; i < 4; ++i)
        b.wire(b.add("math.constant", { 0.0f }), b.add("sink.voiceGain", { static_cast<float>(i) }));

    NodeId partial0 = b.add("math.constant", { 1.0f });
    b.wire(partial0, b.add("sink.partialAmp", { 0.0f }));
    for (int i = 1; i < 6; ++i)
        b.wire(b.add("math.constant", { 0.0f }), b.add("sink.partialAmp", { static_cast<float>(i) }));

    for (int i = 0; i < 4; ++i) {
        b.wire(b.add("math.constant", { 0.5f }), b.add("sink.panL", { static_cast<float>(i) }));
        b.wire(b.add("math.constant", { 0.5f }), b.add("sink.panR", { static_cast<float>(i) }));
    }

    b.wire(b.add("math.constant", { 0.0f }), b.add("sink.chordSemitone", { 0.0f }));
    b.wire(b.add("math.constant", { 0.0f }), b.add("sink.chordSemitone", { 1.0f }));
    b.wire(b.add("math.constant", { 0.0f }), b.add("sink.chordSemitone", { 2.0f }));

    b.wire(b.add("math.constant", { 0.0f }), b.add("sink.driveAmt"));
    b.wire(b.add("math.constant", { 0.0f }), b.add("sink.vibratoDepth"));
    b.wire(b.add("math.constant", { 5.0f }), b.add("sink.vibratoRateHz"));
    b.wire(b.add("math.constant", { 0.0f }), b.add("sink.tremoloDepth"));
    b.wire(b.add("math.constant", { 4.0f }), b.add("sink.tremoloRateHz"));
    b.wire(b.add("math.constant", { 0.0f }), b.add("sink.noiseAmount"));
    b.wire(b.add("math.constant", { 0.5f }), b.add("sink.noiseLpCoef"));

    return graph;
}

} // namespace Graph::Presets
