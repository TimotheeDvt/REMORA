#include "AllPresets.h"
#include "PresetHelpers.h"

// Pitch angle sets the glottal pitch; roll/yaw shape the vowel (tongue height/front-back position);
// motion energy (gyro + Laban Weight) drives volume and vocal tenseness; sharp thrust jabs pinch a
// constriction near the lips for consonant-like bursts.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildVocalTract() {
    constexpr float kPi = 3.14159265f;
    constexpr float kGyroscopeFloor = 30.0f;

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    NodeId synth = addPinkTromboneSynth(b);
    NodeId gain = addGeneralGain(b);
    b.wire(constantNode(b, 1.0f), synth, PinkTrombonePort::Level);

    NodeId pitch = b.add("source.pitch");
    NodeId freqHz = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, 90.0f, 300.0f });
    b.wire(pitch, freqHz).wire(freqHz, synth, PinkTrombonePort::FrequencyHz);

    NodeId roll = b.add("source.roll");
    NodeId tongueHeight = b.add("math.mapRange", { -kPi, kPi, 1.7f, 3.3f });
    b.wire(roll, tongueHeight).wire(tongueHeight, synth, PinkTrombonePort::TongueDiameter);

    NodeId yaw = b.add("source.yaw");
    NodeId tonguePos = b.add("math.mapRange", { -kPi, kPi, 0.0f, 1.0f });
    b.wire(yaw, tonguePos).wire(tonguePos, synth, PinkTrombonePort::TongueIndexNorm);

    NodeId labanWeight = b.add("source.labanWeight");
    NodeId tenseness = b.add("math.mapRange", { 0.0f, 1.0f, 0.3f, 1.0f });
    b.wire(labanWeight, tenseness).wire(tenseness, synth, PinkTrombonePort::Tenseness);

    NodeId gyroMag = b.add("source.gyroMagnitude");
    NodeId thrustPeak = b.add("source.thrustPeakEnvelope");
    NodeId motionGate = standardMotionGate(b, gyroMag, kGyroscopeFloor);
    b.wire(standardMasterGain(b, motionGate, labanWeight, thrustPeak), gain, 0);

    // A jab pinches the tract near the front (a fixed spot, toward the lips) and adds turbulence -
    // a plosive/fricative-ish burst rather than a smooth vowel.
    b.wire(constantNode(b, 0.85f), synth, PinkTrombonePort::ConstrictionIndexNorm);
    NodeId constrictionDiam = b.add("math.mapRange", { 0.0f, 1.0f, 4.0f, 0.5f });
    b.wire(thrustPeak, constrictionDiam).wire(constrictionDiam, synth, PinkTrombonePort::ConstrictionDiameter);
    b.wire(clampNode(b, thrustPeak, 0.0f, 1.0f), synth, PinkTrombonePort::FricativeIntensity);

    return graph;
}

} // namespace Graph::Presets
