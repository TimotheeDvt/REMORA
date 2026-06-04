#pragma once

#include <JuceHeader.h>

inline void styleLabel(juce::Label &l, const juce::String &text, float fontSize,
                       juce::Colour colour,
                       juce::Justification just = juce::Justification::centredLeft) {
  l.setText(text, juce::dontSendNotification);
  l.setFont(juce::Font(juce::FontOptions().withHeight(fontSize)));
  l.setColour(juce::Label::textColourId, colour);
  l.setJustificationType(just);
}