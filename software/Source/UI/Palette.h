#pragma once

#include <JuceHeader.h>
#include <initializer_list>
#include <utility>

namespace Palette {
  const juce::Colour bg{0xFF2A2A2A};
  const juce::Colour panel{0xFF333333};
  const juce::Colour border{0xFF555555};
  const juce::Colour accent{0xFF5B9BD5};
  const juce::Colour accentDim{0xFF3A5A7A};
  const juce::Colour textHi{0xFFFFFFFF};
  const juce::Colour textMid{0xFFCCCCCC};
  const juce::Colour textLo{0xFF999999};
  const juce::Colour red{0xFFD9534F};
  const juce::Colour yellow{0xFFF0AD4E};
  const juce::Colour green{0xFF5CB85C};

  // Reusable colourId/Colour groups for styleButton()
  namespace ButtonTheme {
    inline const std::initializer_list<std::pair<int, juce::Colour>> secondary = {
        {juce::TextButton::buttonColourId, panel},
        {juce::TextButton::textColourOffId, textMid}};
  }
}