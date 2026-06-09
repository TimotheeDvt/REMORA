#pragma once

#include "../DSP/MathHelpers.h"
#include <JuceHeader.h>

struct OrientationPoint {
  MathHelpers::Quat orientation;
  juce::uint32 timestamp;
};