#include "AllPresets.h"
#include "PresetHelpers.h"
#include <vector>

// Pentatonic root+5th quantized from raw gyro magnitude, roll-driven LPF taper.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildSpinFilter() {
    static const int kPentatonic[5] = { 0, 3, 5, 7, 10 };
    // scaleStep = clamp(gyroMag/50, 0, 10), indexed 0..10 directly - no negative-step correction needed.
    std::vector<float> scaleTable;
    for (int step = 0; step <= 10; ++step) {
        const int octave = step / 5;
        const int degree = step % 5;
        scaleTable.push_back(static_cast<float>(octave * 12 + kPentatonic[degree]));
    }

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);

    NodeId gyroMag = b.add("source.gyroMagnitudeRaw");
    NodeId accelMag = b.add("source.accelMagnitudeRaw");

    NodeId scaleIndex = clampNode(b, scale(b, gyroMag, 1.0f / 50.0f), 0.0f, 10.0f);
    NodeId semitones = b.add("math.lookupTable", scaleTable);
    b.wire(scaleIndex, semitones);

    NodeId rootHz = b.add("math.semitonesToHz", { 130.81f });
    b.wire(semitones, rootHz);
    b.wire(rootHz, synth, AdditivePort::RootHz);

    b.wire(constantNode(b, 2.0f), synth, AdditivePort::NumVoices);
    // chordSemitone[0]=0 matches the Additive Synth's own default - omitted.
    b.wire(constantNode(b, 7.0f), synth, AdditivePort::ChordSemitone1);

    NodeId pitch = b.add("source.pitch");
    NodeId pitchNorm = clampNode(b, scale(b, addConst(b, pitch, 1.57f), 1.0f / 3.14f), 0.0f, 1.0f);
    b.wire(scale(b, pitchNorm, 0.025f), synth, AdditivePort::VibratoDepth);
    b.wire(addConst(b, scale(b, accelMag, 0.1f), 4.0f), synth, AdditivePort::VibratoRateHz);

    NodeId yaw = b.add("source.yaw");
    NodeId yawNorm = clampNode(b, scale(b, addConst(b, yaw, 3.14f), 1.0f / 6.28f), 0.0f, 1.0f);
    b.wire(subNodes(b, constantNode(b, 1.0f), yawNorm), synth, AdditivePort::PanL0);
    b.wire(yawNorm, synth, AdditivePort::PanR0);
    b.wire(yawNorm, synth, AdditivePort::PanL1);
    b.wire(subNodes(b, constantNode(b, 1.0f), yawNorm), synth, AdditivePort::PanR1);

    NodeId energy = clampNode(b, addNodes(b, scale(b, gyroMag, 0.01f), scale(b, accelMag, 0.05f)), 0.0f, 1.0f);
    b.wire(addConst(b, scale(b, energy, 0.8f), 0.05f), gain, 0);
    b.wire(constantNode(b, 0.9f), synth, AdditivePort::VoiceGain0);
    b.wire(constantNode(b, 0.7f), synth, AdditivePort::VoiceGain1);

    NodeId roll = b.add("source.roll");
    NodeId rollNorm = clampNode(b, scale(b, addConst(b, roll, 3.14f), 1.0f / 6.28f), 0.0f, 1.0f);
    NodeId maxActivePartial = addConst(b, scale(b, rollNorm, 5.0f), 1.0f);

    // Per-partial gain = 1/p below the cutoff, a 1-wide linear taper through it, 0 above.
    for (int p = 1; p <= 6; ++p) {
        NodeId taper = clampNode(b, addConst(b, subNodes(b, maxActivePartial, constantNode(b, static_cast<float>(p))), 1.0f), 0.0f, 1.0f);
        b.wire(scale(b, taper, 1.0f / static_cast<float>(p)), synth, AdditivePort::PartialAmp0 + (p - 1));
    }

    b.wire(scale(b, energy, 0.15f), synth, AdditivePort::NoiseAmount);
    b.wire(addConst(b, scale(b, rollNorm, 0.5f), 0.05f), synth, AdditivePort::NoiseLpCoef);
    b.wire(scale(b, energy, 1.5f), synth, AdditivePort::DriveAmt);

    return graph;
}

} // namespace Graph::Presets
