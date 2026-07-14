#pragma once

#include <JuceHeader.h>
#include <functional>
#include <initializer_list>
#include <utility>

inline void styleLabel(juce::Label &l, const juce::String &text, float fontSize,
                       juce::Colour colour,
                       juce::Justification just = juce::Justification::centredLeft) {
  l.setText(text, juce::dontSendNotification);
  l.setFont(juce::Font(juce::FontOptions().withHeight(fontSize)));
  l.setColour(juce::Label::textColourId, colour);
  l.setJustificationType(just);
}

inline void styleButton(juce::TextButton &b, const juce::String &text,
                        std::initializer_list<std::pair<int, juce::Colour>> colours,
                        std::function<void()> onClick = nullptr) {
  b.setButtonText(text);
  for (auto &[colourId, colour] : colours)
    b.setColour(colourId, colour);
  if (onClick)
    b.onClick = std::move(onClick);
}