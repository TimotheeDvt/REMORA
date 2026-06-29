#include "BoStaffVisualizer.h"
#include "Palette.h"

BoStaffVisualizer::BoStaffVisualizer() {
  tip1Points.reserve(512);
  tip2Points.reserve(512);
  paintTimestamps.reserve(512);
}

BoStaffVisualizer::~BoStaffVisualizer() {}

void BoStaffVisualizer::setTrailLifetime(float lifetimeInSeconds) {
  trailLifetimeMs = lifetimeInSeconds * 1000.0f;
}

void BoStaffVisualizer::updateStaff(
    MathHelpers::Quat q, const std::vector<OrientationPoint> &history) {
  currentQuat = q;
  orientationHistory = history;
  repaint();
}

void BoStaffVisualizer::drawTrail(juce::Graphics &g,
                                  const std::vector<juce::Point<float>> &points,
                                  const std::vector<juce::uint32> &timestamps,
                                  juce::uint32 currentTime,
                                  juce::Colour colour) {
  if (points.size() < 2)
    return;

  for (size_t i = 1; i < points.size(); ++i) {
    auto p1 = points[i - 1];
    auto p2 = points[i];

    float ageMs = static_cast<float>(currentTime - timestamps[i]);
    float lifeRatio = 1.0f - juce::jlimit(0.0f, 1.0f, ageMs / trailLifetimeMs);

    g.setColour(colour.withAlpha(lifeRatio));
    g.drawLine(juce::Line<float>(p1, p2), 1.0f + (6.0f * lifeRatio));
  }
}

static juce::Point<float> project(MathHelpers::Vec3 v, float cx, float cy, float scale) {
  constexpr float depth = 0.3f;
  return juce::Point<float>(cx + (v.x - v.y * depth) * scale,
                            cy - (v.z - v.y * depth) * scale);
}

void BoStaffVisualizer::paint(juce::Graphics &g) {
  const float w = static_cast<float>(getWidth());
  const float h = static_cast<float>(getHeight());
  const float scale = juce::jmin(w, h) * 0.4f;
  const float cx = w / 2.f;
  const float cy = h / 2.f;

  // 1. Draw Axis Reference
  auto pOrigin = project({0.f, 0.f, 0.f}, cx, cy, scale);
  auto pX = project({1.f, 0.f, 0.f}, cx, cy, scale);
  auto pY = project({0.f, 1.f, 0.f}, cx, cy, scale);
  auto pZ = project({0.f, 0.f, 1.f}, cx, cy, scale);

  g.setColour(Palette::red.withAlpha(0.6f));
  g.drawLine(pOrigin.x, pOrigin.y, pX.x, pX.y, 2.f);
  g.drawText("X", juce::Rectangle<float>(pX.x - 10.f, pX.y - 10.f, 20.f, 20.f),
             juce::Justification::centred);

  g.setColour(Palette::green.withAlpha(0.6f));
  g.drawLine(pOrigin.x, pOrigin.y, pY.x, pY.y, 2.f);
  g.drawText("Y", juce::Rectangle<float>(pY.x - 10.f, pY.y - 10.f, 20.f, 20.f),
             juce::Justification::centred);

  g.setColour(Palette::accent.withAlpha(0.6f));
  g.drawLine(pOrigin.x, pOrigin.y, pZ.x, pZ.y, 2.f);
  g.drawText("Z", juce::Rectangle<float>(pZ.x - 10.f, pZ.y - 10.f, 20.f, 20.f),
             juce::Justification::centred);

  // 2. Draw the Trails
  auto now = juce::Time::getMillisecondCounter();

  tip1Points.clear();
  tip2Points.clear();
  paintTimestamps.clear();

  if (!orientationHistory.empty()) {
    for (const auto &pt : orientationHistory) {
      MathHelpers::Vec3 t = {1.f, 0.f, 0.f};
      MathHelpers::Vec3 b = {-1.f, 0.f, 0.f};
      tip1Points.push_back(project(MathHelpers::rotate(b, pt.orientation), cx, cy, scale));
      tip2Points.push_back(project(MathHelpers::rotate(t, pt.orientation), cx, cy, scale));
      paintTimestamps.push_back(pt.timestamp);
    }

    drawTrail(g, tip1Points, paintTimestamps, now, Palette::accentDim);
    drawTrail(g, tip2Points, paintTimestamps, now, Palette::red);
  }

  // 3. Draw the active Staff over the trails
  MathHelpers::Vec3 top = {1.f, 0.f, 0.f};
  MathHelpers::Vec3 bottom = {-1.f, 0.f, 0.f};

  top = MathHelpers::rotate(top, currentQuat);
  bottom = MathHelpers::rotate(bottom, currentQuat);

  auto pTop = project(top, cx, cy, scale);
  auto pBot = project(bottom, cx, cy, scale);

  g.setColour(Palette::textMid);
  g.drawLine(pBot.x, pBot.y, pTop.x, pTop.y, 8.f);

  g.setColour(Palette::accentDim);
  g.fillEllipse(pBot.x - 6.f, pBot.y - 6.f, 12.f, 12.f);
  g.setColour(Palette::red);
  g.fillEllipse(pTop.x - 6.f, pTop.y - 6.f, 12.f, 12.f);
}