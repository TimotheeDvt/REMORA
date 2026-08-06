#include "../GraphBuilder.h"
#include "AllPresets.h"

// One voice, root pitch from staff pitch angle, gain from |roll|.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildSimple() {
    constexpr float kPi = 3.14159265f;

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    NodeId pitch = b.add("source.pitch");
    NodeId rootHz = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, 100.0f, 1000.0f });
    NodeId rootSink = b.add("sink.rootHz");
    b.wire(pitch, rootHz).wire(rootHz, rootSink);

    NodeId roll = b.add("source.roll");
    NodeId rollAbs = b.add("math.abs");
    NodeId gain = b.add("math.mapRange", { 0.0f, kPi, 0.05f, 0.20f });
    NodeId gainSink = b.add("sink.masterGain");
    b.wire(roll, rollAbs).wire(rollAbs, gain).wire(gain, gainSink);

    // numVoices=1/voiceGain[0]=1/panning-disabled/chordSemitone=0/partialAmp={1,0,0,0,0,0}/
    // vibratoRate=5/tremoloRate=4/noiseLpCoef=0.5 all match the new MappingOutput defaults - omitted.

    return graph;
}

} // namespace Graph::Presets
