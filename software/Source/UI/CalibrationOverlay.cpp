#include "CalibrationOverlay.h"
#include "Palette.h"

CalibrationOverlay::CalibrationOverlay() { setInterceptsMouseClicks(true, false); }

CalibrationOverlay::~CalibrationOverlay() {}

void CalibrationOverlay::setStepText(const juce::String &title,
                                     const juce::String &hint,
                                     juce::Colour colour) {
  titleText = title;
  hintText = hint;
  hintColour = colour;
  repaint();
}

void CalibrationOverlay::mouseDown(const juce::MouseEvent &) {
  isMouseDown = true;
  repaint();
}

void CalibrationOverlay::mouseUp(const juce::MouseEvent &e) {
  isMouseDown = false;
  repaint();
  if (getLocalBounds().contains(e.getPosition()) && onClicked)
    onClicked();
}

void CalibrationOverlay::paint(juce::Graphics &g) {
  auto bounds = getLocalBounds().toFloat();

  g.setColour(Palette::panel.brighter(isMouseDown ? 0.08f : 0.0f));
  g.fillRoundedRectangle(bounds, 10.f);

  g.setColour(hintColour);
  g.drawRoundedRectangle(bounds.reduced(2.f), 10.f, 2.f);

  const float h = bounds.getHeight();

  g.setColour(Palette::textHi);
  g.setFont(juce::Font(juce::FontOptions().withHeight(juce::jmin(64.f, h * 0.22f)).withStyle("Bold")));
  g.drawFittedText(titleText, bounds.reduced(20.f, 10.f).removeFromTop(h * 0.4f).toNearestInt(),
                   juce::Justification::centred, 1);

  g.setColour(hintColour);
  g.setFont(juce::Font(juce::FontOptions().withHeight(juce::jmin(28.f, h * 0.09f))));
  g.drawFittedText(hintText, bounds.reduced(30.f, 10.f).removeFromBottom(h * 0.35f).toNearestInt(),
                   juce::Justification::centred, 2);

  g.setColour(Palette::textLo);
  g.setFont(juce::Font(juce::FontOptions().withHeight(14.f)));
  g.drawText("CLICK TO CONFIRM POSE", bounds.reduced(10.f).toNearestInt(),
             juce::Justification::centredBottom);
}
