#pragma once

#include "../GraphBuilder.h"

namespace Graph::Presets {

struct AzimutCoreOutputs {
    NodeId rootHz;
    NodeId chordSemitone[3];
    NodeId numVoices;
    NodeId voiceGain[4];
    NodeId masterGain;
    NodeId panL[4];
    NodeId panR[4];

    // Exposed so variant builders can derive their own LPF/modulation/reverb logic - the one place they differ.
    NodeId labanWeight;
    NodeId gyroMagnitude;
    NodeId spinClassNode; // source.spinClassification node; port 2 is the continuous spin count.
};

AzimutCoreOutputs buildAzimutCore(GraphBuilder& b);

struct AzimutTimbreOutputs {
    NodeId partialAmp[6];
    NodeId driveAmt;
    NodeId noiseAmount;
    NodeId noiseLpCoef;
    NodeId flowFree;
};

AzimutTimbreOutputs buildAzimutTimbre(GraphBuilder& b, const AzimutCoreOutputs& core);

// sin(continuousSpinCount * 1.5) -> 400..20000 Hz, smoothed at a fixed 0.03 one-pole rate.
NodeId buildSpinCountLpfHz(GraphBuilder& b, const AzimutCoreOutputs& core);

} // namespace Graph::Presets
