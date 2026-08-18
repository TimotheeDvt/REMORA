#include "AllPresets.h"
#include "PresetHelpers.h"

// Pure wind: no pitch, no chord - staff motion energy alone shapes filtered noise
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildWindNoise() {
    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);
    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);

    NodeId gyroMag = b.add("source.gyroMagnitudeRaw");
    NodeId accelMag = b.add("source.accelMagnitudeRaw");

    // energy = clamp(gyroMag/500 + accelMag*0.15, 0, 1), smoothed so gusts swell and die down rather than snap.
    NodeId energyRaw = clampNode(b, addNodes(b, scale(b, gyroMag, 1.0f / 500.0f), scale(b, accelMag, 0.15f)), 0.0f, 1.0f);
    NodeId energy = onePole(b, energyRaw, 0.12f);

    b.wire(constantNode(b, 1.0f), synth, AdditivePort::NumVoices);
    b.wire(constantNode(b, 0.0f), synth, AdditivePort::PartialAmp0); // kill the sine fundamental - noise only, no pitch
    b.wire(constantNode(b, 0.5f), gain, 0);

    b.wire(scale(b, energy, 0.9f), synth, AdditivePort::NoiseAmount);
    b.wire(constantNode(b, 1.0f), synth, AdditivePort::UsePinkNoise);

    NodeId noiseCoef = b.add("math.mapRange", { 0.0f, 1.0f, 0.97f, 0.75f });
    b.wire(energy, noiseCoef);
    b.wire(noiseCoef, synth, AdditivePort::NoiseLpCoef);

    // Master filter opens up with motion, like wind rushing past faster - logarithmic since it's a frequency.
    NodeId lpf = b.add("math.mapRangeLog", { 0.0f, 1.0f, 250.0f, 12000.0f });
    b.wire(energy, lpf);
    b.wire(lpf, synth, AdditivePort::LpfCutoffHz);

    // A big, airy reverb tail for open-air space.
    b.wire(constantNode(b, 0.28f), synth, AdditivePort::ReverbWetLevel);
    b.wire(constantNode(b, 0.85f), synth, AdditivePort::ReverbRoomSize);
    b.wire(constantNode(b, 0.18f), synth, AdditivePort::ReverbDamping);

    return graph;
}

} // namespace Graph::Presets
