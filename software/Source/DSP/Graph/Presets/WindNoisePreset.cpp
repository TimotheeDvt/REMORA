#include "AllPresets.h"
#include "PresetHelpers.h"

// Pure wind: no pitch, no chord - staff motion energy alone shapes filtered noise
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildWindNoise() {
    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    NodeId gyroMag = b.add("source.gyroMagnitudeRaw");
    NodeId accelMag = b.add("source.accelMagnitudeRaw");

    // energy = clamp(gyroMag/500 + accelMag*0.15, 0, 1), smoothed so gusts swell and die down rather than snap.
    NodeId energyRaw = clampNode(b, addNodes(b, scale(b, gyroMag, 1.0f / 500.0f), scale(b, accelMag, 0.15f)), 0.0f, 1.0f);
    NodeId energy = onePole(b, energyRaw, 0.12f);

    toSink(b, constantNode(b, 1.0f), "sink.numVoices");
    toSink(b, constantNode(b, 0.0f), "sink.partialAmp", { 0.0f }); // kill the sine fundamental - noise only, no pitch
    toSink(b, constantNode(b, 0.5f), "sink.masterGain");

    toSink(b, scale(b, energy, 0.9f), "sink.noiseAmount");
    toSink(b, constantNode(b, 1.0f), "sink.usePinkNoise");

    NodeId noiseCoef = b.add("math.mapRange", { 0.0f, 1.0f, 0.97f, 0.75f });
    b.wire(energy, noiseCoef);
    toSink(b, noiseCoef, "sink.noiseLpCoef");

    // Master filter opens up with motion, like wind rushing past faster - logarithmic since it's a frequency.
    NodeId lpf = b.add("math.mapRangeLog", { 0.0f, 1.0f, 250.0f, 12000.0f });
    b.wire(energy, lpf);
    toSink(b, lpf, "sink.lpfCutoffHz");

    // A little reverb for open-air space.
    toSink(b, constantNode(b, 0.18f), "sink.reverbWetLevel");
    toSink(b, constantNode(b, 0.6f), "sink.reverbRoomSize");
    toSink(b, constantNode(b, 0.35f), "sink.reverbDamping");

    return graph;
}

} // namespace Graph::Presets
