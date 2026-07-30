#pragma once

#include "../GraphBuilder.h"

// Shared body-motion mapping logic behind Azimut / Azimut+ / Azimut Reverb
// (and reused, via node ids rather than sinks, by Ben's - see BensPreset.cpp).
// Returns node ids rather than attaching sinks itself, so callers can either
// wire them straight to sinks (the 3 Azimut variants) or crossfade them
// against another branch first (Ben's).
namespace Graph::Presets {

struct AzimutCoreOutputs {
    NodeId rootHz;
    NodeId chordSemitone[3];
    NodeId numVoices;
    NodeId voiceGain[4];
    NodeId masterGain;
    NodeId partialAmp[6];
    NodeId driveAmt;
    NodeId noiseAmount;
    NodeId noiseLpCoef;
    NodeId panL[4];
    NodeId panR[4];

    // Exposed so variant builders can each derive their own LPF
    // cutoff/modulation/reverb logic (the one place the 3 variants differ).
    NodeId labanWeight;
    NodeId flowBound;
    NodeId flowFree;
    NodeId gyroMagnitude;
    NodeId continuousSpinCount;
};

AzimutCoreOutputs buildAzimutCore(GraphBuilder& b);

// sin(continuousSpinCount * 1.5) -> 400..20000 Hz, smoothed at a fixed 0.03
// one-pole rate. Shared by plain Azimut, Azimut Reverb, and Ben's (which
// blends it against its own "simple melody" lpfCutoffHz).
NodeId buildSpinCountLpfHz(GraphBuilder& b, const AzimutCoreOutputs& core);

} // namespace Graph::Presets
