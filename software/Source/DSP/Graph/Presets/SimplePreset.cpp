#include "../GraphBuilder.h"
#include "AllPresets.h"
#include "PresetHelpers.h"

// One voice, root pitch from staff pitch angle, gain from |roll|.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildSimple() {
    constexpr float kPi = 3.14159265f;

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);

    NodeId pitch = b.add("source.pitch");
    NodeId rootHz = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, 100.0f, 1000.0f });
    b.wire(pitch, rootHz).wire(rootHz, synth, AdditivePort::RootHz);

    NodeId roll = b.add("source.roll");
    NodeId rollAbs = b.add("math.abs");
    NodeId gainAmt = b.add("math.mapRange", { 0.0f, kPi, 0.05f, 0.20f });
    b.wire(roll, rollAbs).wire(rollAbs, gainAmt).wire(gainAmt, gain, 0);

    // numVoices=1/voiceGain[0]=1/panning-disabled/chordSemitone=0/partialAmp={1,0,0,0,0,0}/
    // vibratoRate=5/tremoloRate=4/noiseLpCoef=0.5 all match the Additive Synth node's own port
    // defaults - omitted.

    return graph;
}

} // namespace Graph::Presets
