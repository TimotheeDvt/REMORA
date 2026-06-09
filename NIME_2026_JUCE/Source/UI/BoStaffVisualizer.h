#pragma once

#include "../DSP/MathHelpers.h"
#include "../Data/OrientationPoint.h"
#include <JuceHeader.h>

class BoStaffVisualizer : public juce::Component {
public:
  BoStaffVisualizer();
  ~BoStaffVisualizer() override;

  void paint(juce::Graphics &) override;
  void updateStaff(MathHelpers::Quat q,
                   const std::vector<OrientationPoint> &history);
  void setTrailLifetime(float lifetimeInSeconds);
  float getTrailLifetimeMs() const { return trailLifetimeMs; }

private:
  std::vector<OrientationPoint> orientationHistory;
  float trailLifetimeMs = 500.0f;

  MathHelpers::Quat currentQuat{1.f, 0.f, 0.f, 0.f};

  std::vector<juce::Point<float>> tip1Points, tip2Points;
  std::vector<juce::uint32> paintTimestamps;

  void drawTrail(juce::Graphics &g,
                 const std::vector<juce::Point<float>> &points,
                 const std::vector<juce::uint32> &timestamps,
                 juce::uint32 currentTime, juce::Colour colour);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoStaffVisualizer)
};