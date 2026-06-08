#pragma once

#include "../DSP/MathHelpers.h"
#include <JuceHeader.h>


class BoStaffVisualizer : public juce::Component {
public:
  BoStaffVisualizer();
  ~BoStaffVisualizer() override;

  void paint(juce::Graphics &) override;
  void updateStaff(MathHelpers::Quat q);
  void setTrailLifetime(float lifetimeInSeconds);

private:
  struct TracePoint {
    juce::Point<float> position;
    juce::uint32 timestamp;
  };

  std::vector<TracePoint> tip1History;
  std::vector<TracePoint> tip2History;
  float trailLifetimeMs = 500.0f;

  MathHelpers::Quat currentQuat{1.f, 0.f, 0.f, 0.f};

  void drawTrail(juce::Graphics &g, const std::vector<TracePoint> &history,
                 juce::uint32 currentTime, juce::Colour colour);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoStaffVisualizer)
};