#pragma once

#include <JuceHeader.h>

// Big, unmissable calibration panel shown in place of the staff visualizer
// until calibration is complete. The whole panel acts as the button.
class CalibrationOverlay : public juce::Component {
public:
  CalibrationOverlay();
  ~CalibrationOverlay() override;

  void paint(juce::Graphics &) override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseUp(const juce::MouseEvent &) override;

  void setStepText(const juce::String &title, const juce::String &hint,
                   juce::Colour hintColour);

  std::function<void()> onClicked;

private:
  juce::String titleText;
  juce::String hintText;
  juce::Colour hintColour{juce::Colours::white};
  bool isMouseDown = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CalibrationOverlay)
};
