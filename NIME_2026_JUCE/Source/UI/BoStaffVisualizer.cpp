#include "BoStaffVisualizer.h"
#include "Palette.h"

BoStaffVisualizer::BoStaffVisualizer() {}

BoStaffVisualizer::~BoStaffVisualizer() {}

void BoStaffVisualizer::setTrailLifetime(float lifetimeInSeconds) {
  trailLifetimeMs = lifetimeInSeconds * 1000.0f;
}

void BoStaffVisualizer::updateStaff(MathHelpers::Quat q) {
  currentQuat = q;

  const float w = getWidth();
  const float h = getHeight();
  const float scale = 90.f;
  const float cx = w / 2.f;
  const float cy = h / 2.f;

  auto project = [cx, cy, scale](MathHelpers::Vec3 v) {
    const float depth = 0.3f;
    return juce::Point<float>(cx + (v.x - v.y * depth) * scale,
                              cy - (v.z - v.y * depth) * scale);
  };

  MathHelpers::Vec3 top = {1.f, 0.f, 0.f};
  MathHelpers::Vec3 bottom = {-1.f, 0.f, 0.f};

  top = MathHelpers::rotate(top, q);
  bottom = MathHelpers::rotate(bottom, q);

  auto pTop = project(top);
  auto pBot = project(bottom);

  auto now = juce::Time::getMillisecondCounter();
  tip1History.push_back({pBot, now});
  tip2History.push_back({pTop, now});

  // Remove expired points to fade out the trails
  auto isOld = [&](const TracePoint &p) { return (now - p.timestamp) > trailLifetimeMs; };
  tip1History.erase(std::remove_if(tip1History.begin(), tip1History.end(), isOld), tip1History.end());
  tip2History.erase(std::remove_if(tip2History.begin(), tip2History.end(), isOld), tip2History.end());

  repaint();
}

void BoStaffVisualizer::drawTrail(juce::Graphics &g, const std::vector<TracePoint> &history, juce::uint32 currentTime, juce::Colour colour) {
  if (history.size() < 2) return;
  
  for (size_t i = 1; i < history.size(); ++i) {
    auto &p1 = history[i - 1];
    auto &p2 = history[i];
    
    float ageMs = static_cast<float>(currentTime - p2.timestamp);
    float lifeRatio = 1.0f - juce::jlimit(0.0f, 1.0f, ageMs / trailLifetimeMs);
    
    g.setColour(colour.withAlpha(lifeRatio));
    g.drawLine(juce::Line<float>(p1.position, p2.position), 1.0f + (6.0f * lifeRatio));
  }
}

void BoStaffVisualizer::paint(juce::Graphics &g) {
  const float w = getWidth();
  const float h = getHeight();
  const float scale = 90.f;
  const float cx = w / 2.f;
  const float cy = h / 2.f;

  auto project = [cx, cy, scale](MathHelpers::Vec3 v) {
    const float depth = 0.3f;
    return juce::Point<float>(cx + (v.x - v.y * depth) * scale,
                              cy - (v.z - v.y * depth) * scale);
  };

  // 1. Draw Axis Reference
  auto pOrigin = project({0.f, 0.f, 0.f});
  auto pX = project({1.f, 0.f, 0.f});
  auto pY = project({0.f, 1.f, 0.f});
  auto pZ = project({0.f, 0.f, 1.f});

  g.setColour(Palette::red.withAlpha(0.6f));
  g.drawLine(pOrigin.x, pOrigin.y, pX.x, pX.y, 2.f);
  g.drawText("X", juce::Rectangle<float>(pX.x - 10.f, pX.y - 10.f, 20.f, 20.f), juce::Justification::centred);

  g.setColour(Palette::green.withAlpha(0.6f));
  g.drawLine(pOrigin.x, pOrigin.y, pY.x, pY.y, 2.f);
  g.drawText("Y", juce::Rectangle<float>(pY.x - 10.f, pY.y - 10.f, 20.f, 20.f), juce::Justification::centred);

  g.setColour(Palette::accent.withAlpha(0.6f));
  g.drawLine(pOrigin.x, pOrigin.y, pZ.x, pZ.y, 2.f);
  g.drawText("Z", juce::Rectangle<float>(pZ.x - 10.f, pZ.y - 10.f, 20.f, 20.f), juce::Justification::centred);

  // 2. Draw the Trails
  auto now = juce::Time::getMillisecondCounter();
  drawTrail(g, tip1History, now, Palette::accentDim);
  drawTrail(g, tip2History, now, Palette::red);
  
  // 3. Draw the active Staff over the trails
  MathHelpers::Vec3 top = {1.f, 0.f, 0.f};
  MathHelpers::Vec3 bottom = {-1.f, 0.f, 0.f};

  top = MathHelpers::rotate(top, currentQuat);
  bottom = MathHelpers::rotate(bottom, currentQuat);

  auto pTop = project(top);
  auto pBot = project(bottom);

  g.setColour(Palette::textMid);
  g.drawLine(pBot.x, pBot.y, pTop.x, pTop.y, 8.f);

  g.setColour(Palette::accentDim);
  g.fillEllipse(pBot.x - 6.f, pBot.y - 6.f, 12.f, 12.f);
  g.setColour(Palette::red);
  g.fillEllipse(pTop.x - 6.f, pTop.y - 6.f, 12.f, 12.f);
}