#pragma once

// Port indices for the per-engine mega-sink nodes ("sink.additiveSynth", "sink.granularSynth").
// Shared between NodeMetadata.cpp (registration + SinkWriteFn) and every preset builder (wiring).
namespace Graph {

namespace AdditivePort {
enum : int {
    RootHz, NumVoices, DriveAmt, VibratoDepth, VibratoRateHz, TremoloDepth, TremoloRateHz,
    NoiseAmount, NoiseLpCoef, UsePinkNoise, LpfCutoffHz, UseIndependentVoicePitch,
    ReverbWetLevel, ReverbRoomSize, ReverbDamping,
    ChordSemitone0, ChordSemitone1, ChordSemitone2,
    VoiceGain0, VoiceGain1, VoiceGain2, VoiceGain3,
    PartialAmp0, PartialAmp1, PartialAmp2, PartialAmp3, PartialAmp4, PartialAmp5,
    PanL0, PanL1, PanL2, PanL3,
    PanR0, PanR1, PanR2, PanR3,
    VoiceHz0, VoiceHz1, VoiceHz2, VoiceHz3,
    Count
};
} // namespace AdditivePort

namespace GranularPort {
enum : int {
    Position, PositionSpray, GrainSizeMs, DensityHz, PitchSemitones, PitchSpray,
    PanSpread, AmpSpray, Level, ReverseAmount,
    Count
};
} // namespace GranularPort

} // namespace Graph
