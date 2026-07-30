#include "AllPresets.h"
#include "AzimutCore.h"
#include "PresetHelpers.h"

// Reproduces the retired BensMapping: a simple pentatonic-melody branch
// cross-faded against a full copy of the Azimut branch (via buildAzimutCore(),
// the same shared logic plain Azimut/Azimut+/Azimut Reverb use), gated by the
// staff's own independently-smoothed speed. Below the gate, the simple
// melody plays; above it, Azimut takes over; a speed band around the
// threshold cross-fades between them so crossing it never clicks.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildBens() {
    constexpr float kPi = 3.14159265f;
    constexpr float kRootFrequencyHz = 130.81f; // matches AzimutMapping's C3
    constexpr float kGateSpeedThresholdDegPerSec = 150.0f;
    constexpr float kGateSpeedBandDegPerSec = 60.0f;
    static const float kScaleSemitones[10] = { 0.f, 2.f, 4.f, 7.f, 9.f, 12.f, 14.f, 16.f, 19.f, 21.f };

    auto graph = std::make_unique<NodeGraph>();
    GraphBuilder b(*graph);

    // --- azimutAmount: Ben's own smoothing of raw gyro magnitude, gated
    // across a band around the speed threshold (distinct from Azimut's own
    // internal analyzer-smoothed gyro magnitude - a second, independent
    // smoother, exactly as the original kept its own `smoothed_gyroscope_magnitude_`).
    NodeId gyroRaw = b.add("source.gyroMagnitudeRaw");
    NodeId bensSmoothedGyro = onePole(b, gyroRaw, 0.35f);
    constexpr float kBandStart = kGateSpeedThresholdDegPerSec - kGateSpeedBandDegPerSec * 0.5f;
    NodeId azimutAmount = clampNode(b, scale(b, subNodes(b, bensSmoothedGyro, constantNode(b, kBandStart)), 1.0f / kGateSpeedBandDegPerSec), 0.0f, 1.0f);

    // --- simple melody branch ---
    NodeId pitch = b.add("source.pitch");
    NodeId scaleIndexRaw = b.add("math.mapRange", { -kPi * 0.5f, kPi * 0.5f, 0.0f, 10.0f });
    b.wire(clampNode(b, pitch, -kPi * 0.5f, kPi * 0.5f), scaleIndexRaw);
    NodeId targetSemitones = b.add("math.lookupTable", std::vector<float>(kScaleSemitones, kScaleSemitones + 10));
    b.wire(scaleIndexRaw, targetSemitones);
    NodeId simpleSemitones = onePole(b, targetSemitones, 0.15f);
    NodeId simpleRootHz = b.add("math.semitonesToHz", { kRootFrequencyHz });
    b.wire(simpleSemitones, simpleRootHz);

    NodeId roll = b.add("source.roll");
    NodeId simpleGain = addConst(b, scale(b, clampNode(b, scale(b, absNode(b, roll), 1.0f / kPi), 0.0f, 1.0f), 0.20f), 0.05f);

    NodeId yaw = b.add("source.yaw");
    NodeId yawNorm = clampNode(b, scale(b, yaw, 1.0f / kPi), -1.0f, 1.0f);
    NodeId simplePan = clampNode(b, addConst(b, scale(b, yawNorm, 0.35f), 0.5f), 0.0f, 1.0f);
    NodeId simplePanInv = subConst(b, simplePan, 1.0f);

    const float simpleVoiceGain[4] = { 1.0f, 0.8f, 0.7f, 0.6f };
    const float simplePartials[6] = { 1.0f, 0.5f, 0.25f, 0.1f, 0.05f, 0.02f };
    NodeId simpleChord[3] = { constantNode(b, 12.0f), constantNode(b, 7.0f), constantNode(b, -12.0f) };
    NodeId zero = constantNode(b, 0.0f);

    // --- azimut branch (shared with plain Azimut/Azimut+/Azimut Reverb) ---
    AzimutCoreOutputs azimut = buildAzimutCore(b);
    NodeId azimutLpf = buildSpinCountLpfHz(b, azimut);

    // --- cross-fade every field the original blended (numVoices is NOT
    // blended - it's taken from azimut directly, same as the original) ---
    toSink(b, crossfadeNodes(b, simpleRootHz, azimut.rootHz, azimutAmount), "sink.rootHz");
    for (int i = 0; i < 3; ++i)
        toSink(b, crossfadeNodes(b, simpleChord[i], azimut.chordSemitone[i], azimutAmount), "sink.chordSemitone", { static_cast<float>(i) });
    toSink(b, azimut.numVoices, "sink.numVoices");

    toSink(b, crossfadeNodes(b, simpleGain, azimut.masterGain, azimutAmount), "sink.masterGain");
    for (int i = 0; i < 4; ++i)
        toSink(b, crossfadeNodes(b, constantNode(b, simpleVoiceGain[i]), azimut.voiceGain[i], azimutAmount), "sink.voiceGain", { static_cast<float>(i) });

    for (int i = 0; i < 6; ++i)
        toSink(b, crossfadeNodes(b, constantNode(b, simplePartials[i]), azimut.partialAmp[i], azimutAmount), "sink.partialAmp", { static_cast<float>(i) });
    toSink(b, crossfadeNodes(b, zero, azimut.driveAmt, azimutAmount), "sink.driveAmt");

    toSink(b, crossfadeNodes(b, zero, zero, azimutAmount), "sink.vibratoDepth");
    toSink(b, crossfadeNodes(b, constantNode(b, 5.0f), zero, azimutAmount), "sink.vibratoRateHz");
    toSink(b, crossfadeNodes(b, zero, zero, azimutAmount), "sink.tremoloDepth");
    toSink(b, crossfadeNodes(b, constantNode(b, 4.0f), zero, azimutAmount), "sink.tremoloRateHz");

    toSink(b, crossfadeNodes(b, zero, azimut.noiseAmount, azimutAmount), "sink.noiseAmount");
    toSink(b, crossfadeNodes(b, constantNode(b, 0.5f), azimut.noiseLpCoef, azimutAmount), "sink.noiseLpCoef");

    for (int i = 0; i < 4; ++i) {
        toSink(b, crossfadeNodes(b, simplePan, azimut.panL[i], azimutAmount), "sink.panL", { static_cast<float>(i) });
        toSink(b, crossfadeNodes(b, simplePanInv, azimut.panR[i], azimutAmount), "sink.panR", { static_cast<float>(i) });
    }

    toSink(b, crossfadeNodes(b, constantNode(b, 8000.0f), azimutLpf, azimutAmount), "sink.lpfCutoffHz");

    return graph;
}

} // namespace Graph::Presets
