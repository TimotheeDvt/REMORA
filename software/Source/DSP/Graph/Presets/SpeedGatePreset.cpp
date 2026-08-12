#include "AllPresets.h"
#include "AzimutCore.h"
#include "PresetHelpers.h"

// A pentatonic melody cross-faded against buildAzimutCore() by speed, clickless via a gate band.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildSpeedGate() {
    constexpr float kPi = 3.14159265f;
    constexpr float kRootFrequencyHz = 130.81f; // matches AzimutMapping's C3
    constexpr float kGateSpeedThresholdDegPerSec = 150.0f;
    constexpr float kGateSpeedBandDegPerSec = 60.0f;
    static const float kScaleSemitones[10] = { 0.f, 2.f, 4.f, 7.f, 9.f, 12.f, 14.f, 16.f, 19.f, 21.f };

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    // azimutAmount: this preset's own gyro smoother, independent of Azimut's internal analyzer-smoothed magnitude.
    NodeId gyroRaw = b.add("source.gyroMagnitudeRaw");
    NodeId smoothedGyro = onePole(b, gyroRaw, 0.35f);
    constexpr float kBandStart = kGateSpeedThresholdDegPerSec - kGateSpeedBandDegPerSec * 0.5f;
    NodeId azimutAmount = clampNode(b, scale(b, subNodes(b, smoothedGyro, constantNode(b, kBandStart)), 1.0f / kGateSpeedBandDegPerSec), 0.0f, 1.0f);

    // --- simple melody branch ---
    NodeId pitch = b.add("source.pitch");
    NodeId scaleIndexRaw = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, 0.0f, 10.0f });
    b.wire(pitch, scaleIndexRaw);
    NodeId targetSemitones = b.add("math.lookupTable", std::vector<float>(kScaleSemitones, kScaleSemitones + 10));
    b.wire(scaleIndexRaw, targetSemitones);
    NodeId simpleSemitones = onePole(b, targetSemitones, 0.15f);
    NodeId simpleRootHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
    b.wire(simpleSemitones, simpleRootHz);

    // roll is atan2-derived, already within [-pi, pi] - no clamp needed.
    NodeId roll = b.add("source.roll");
    NodeId simpleGain = addConst(b, scale(b, absNode(b, roll), 0.20f / kPi), 0.05f);

    // yaw is atan2-derived, already within [-pi, pi], so yawNorm is within [-1, 1] and simplePan within
    // [0.15, 0.85] - no clamps needed.
    NodeId yaw = b.add("source.yaw");
    NodeId yawNorm = scale(b, yaw, 1.0f / kPi);
    NodeId simplePan = addConst(b, scale(b, yawNorm, 0.35f), 0.5f);
    NodeId simplePanInv = subConst(b, simplePan, 1.0f);

    const float simpleVoiceGain[4] = { 1.0f, 0.8f, 0.7f, 0.6f };
    const float simplePartials[6] = { 1.0f, 0.5f, 0.25f, 0.1f, 0.05f, 0.02f };
    NodeId simpleChord[3] = { constantNode(b, 12.0f), constantNode(b, 7.0f), constantNode(b, -12.0f) };
    NodeId zero = constantNode(b, 0.0f);

    // --- azimut branch (shared with plain Azimut/Azimut+/Azimut Reverb) ---
    AzimutCoreOutputs azimut = buildAzimutCore(b);
    AzimutTimbreOutputs azimutTimbre = buildAzimutTimbre(b, azimut);
    NodeId azimutLpf = buildSpinCountLpfHz(b, azimut);

    NodeId synth = addAdditiveSynth(b);
    NodeId gain = addGeneralGain(b);

    // numVoices is NOT cross-faded - taken from azimut directly
    b.wire(crossfadeNodes(b, simpleRootHz, azimut.rootHz, azimutAmount), synth, AdditivePort::RootHz);
    for (int i = 0; i < 3; ++i)
        b.wire(crossfadeNodes(b, simpleChord[i], azimut.chordSemitone[i], azimutAmount), synth, AdditivePort::ChordSemitone0 + i);
    b.wire(azimut.numVoices, synth, AdditivePort::NumVoices);

    b.wire(crossfadeNodes(b, simpleGain, azimut.masterGain, azimutAmount), gain, 0);
    for (int i = 0; i < 4; ++i)
        b.wire(crossfadeNodes(b, constantNode(b, simpleVoiceGain[i]), azimut.voiceGain[i], azimutAmount), synth, AdditivePort::VoiceGain0 + i);

    // partialAmp[0]: both sides of the crossfade are constant 1.0, matching the Additive Synth's
    // own default regardless of azimutAmount - omitted.
    for (int i = 1; i < 6; ++i)
        b.wire(crossfadeNodes(b, constantNode(b, simplePartials[i]), azimutTimbre.partialAmp[i], azimutAmount), synth, AdditivePort::PartialAmp0 + i);
    b.wire(crossfadeNodes(b, zero, azimutTimbre.driveAmt, azimutAmount), synth, AdditivePort::DriveAmt);

    // No vibrato/tremolo on either side of this crossfade - VibratoDepth/TremoloDepth are left at
    // their own 0 default (never wired below), which alone silences both regardless of rate.

    b.wire(crossfadeNodes(b, zero, azimutTimbre.noiseAmount, azimutAmount), synth, AdditivePort::NoiseAmount);
    b.wire(crossfadeNodes(b, constantNode(b, 0.5f), azimutTimbre.noiseLpCoef, azimutAmount), synth, AdditivePort::NoiseLpCoef);

    for (int i = 0; i < 4; ++i) {
        b.wire(crossfadeNodes(b, simplePan, azimut.panL[i], azimutAmount), synth, AdditivePort::PanL0 + i);
        b.wire(crossfadeNodes(b, simplePanInv, azimut.panR[i], azimutAmount), synth, AdditivePort::PanR0 + i);
    }

    b.wire(crossfadeNodes(b, constantNode(b, 8000.0f), azimutLpf, azimutAmount), synth, AdditivePort::LpfCutoffHz);

    return graph;
}

} // namespace Graph::Presets
