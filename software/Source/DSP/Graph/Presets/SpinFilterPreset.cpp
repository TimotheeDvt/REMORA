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

    NodeId gyroMag = b.add("source.gyroMagnitudeRaw");
    NodeId accelMag = b.add("source.accelMagnitudeRaw");

    NodeId scaleIndex = clampNode(b, scale(b, gyroMag, 1.0f / 50.0f), 0.0f, 10.0f);
    NodeId semitones = b.add("math.lookupTable", scaleTable);
    b.wire(scaleIndex, semitones);

    NodeId rootHz = b.add("math.semitonesToHz", { 130.81f });
    b.wire(semitones, rootHz);
    toSink(b, rootHz, "sink.rootHz");

    toSink(b, constantNode(b, 2.0f), "sink.numVoices");
    // chordSemitone[0]=0 matches the new MappingOutput default - omitted.
    toSink(b, constantNode(b, 7.0f), "sink.chordSemitone", { 1.0f });

    NodeId pitch = b.add("source.pitch");
    NodeId pitchNorm = clampNode(b, scale(b, addConst(b, pitch, 1.57f), 1.0f / 3.14f), 0.0f, 1.0f);
    toSink(b, scale(b, pitchNorm, 0.025f), "sink.vibratoDepth");
    toSink(b, addConst(b, scale(b, accelMag, 0.1f), 4.0f), "sink.vibratoRateHz");

    NodeId yaw = b.add("source.yaw");
    NodeId yawNorm = clampNode(b, scale(b, addConst(b, yaw, 3.14f), 1.0f / 6.28f), 0.0f, 1.0f);
    toSink(b, subNodes(b, constantNode(b, 1.0f), yawNorm), "sink.panL", { 0.0f });
    toSink(b, yawNorm, "sink.panR", { 0.0f });
    toSink(b, yawNorm, "sink.panL", { 1.0f });
    toSink(b, subNodes(b, constantNode(b, 1.0f), yawNorm), "sink.panR", { 1.0f });

    NodeId energy = clampNode(b, addNodes(b, scale(b, gyroMag, 0.01f), scale(b, accelMag, 0.05f)), 0.0f, 1.0f);
    toSink(b, addConst(b, scale(b, energy, 0.8f), 0.05f), "sink.masterGain");
    toSink(b, constantNode(b, 0.9f), "sink.voiceGain", { 0.0f });
    toSink(b, constantNode(b, 0.7f), "sink.voiceGain", { 1.0f });

    NodeId roll = b.add("source.roll");
    NodeId rollNorm = clampNode(b, scale(b, addConst(b, roll, 3.14f), 1.0f / 6.28f), 0.0f, 1.0f);
    NodeId maxActivePartial = addConst(b, scale(b, rollNorm, 5.0f), 1.0f);

    // Per-partial gain = 1/p below the cutoff, a 1-wide linear taper through it, 0 above.
    for (int p = 1; p <= 6; ++p) {
        NodeId taper = clampNode(b, addConst(b, subNodes(b, maxActivePartial, constantNode(b, static_cast<float>(p))), 1.0f), 0.0f, 1.0f);
        toSink(b, scale(b, taper, 1.0f / static_cast<float>(p)), "sink.partialAmp", { static_cast<float>(p - 1) });
    }

    toSink(b, scale(b, energy, 0.15f), "sink.noiseAmount");
    toSink(b, addConst(b, scale(b, rollNorm, 0.5f), 0.05f), "sink.noiseLpCoef");
    toSink(b, scale(b, energy, 1.5f), "sink.driveAmt");

    return graph;
}

} // namespace Graph::Presets
