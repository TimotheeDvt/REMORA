#pragma once

#include "../GraphBuilder.h"

// Shared by Azimut/Azimut+/Azimut Reverb/Speed Gate - returns node ids so callers can wire to sinks or crossfade first.
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

    // Exposed so variant builders can derive their own LPF/modulation/reverb logic - the one place they differ.
    NodeId labanWeight;
    NodeId flowBound;
    NodeId flowFree;
    NodeId gyroMagnitude;
    NodeId spinClassNode; // source.spinClassification node; port 2 is the continuous spin count.
};

AzimutCoreOutputs buildAzimutCore(GraphBuilder& b);

// sin(continuousSpinCount * 1.5) -> 400..20000 Hz, smoothed at a fixed 0.03 one-pole rate.
NodeId buildSpinCountLpfHz(GraphBuilder& b, const AzimutCoreOutputs& core);

} // namespace Graph::Presets
