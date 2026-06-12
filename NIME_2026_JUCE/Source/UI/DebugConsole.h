#pragma once

#include <JuceHeader.h>

class DebugConsole : public juce::DocumentWindow
{
public:
    DebugConsole(const juce::String& name);
    ~DebugConsole() override;

    void closeButtonPressed() override;

    void addMessage(const juce::String& message, juce::Colour colour);
    void clear();

private:
    class Content;
    Content* contentComponent = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugConsole)
};