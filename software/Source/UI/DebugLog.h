#pragma once

#include <JuceHeader.h>
#include <sstream>

class DebugConsole;

class DebugLog : public juce::ChangeBroadcaster
{
public:
    static DebugLog& getInstance();

    void show();
    void hide();
    bool isWindowOpen() const;
    juce::Rectangle<int> getBounds() const;
    void setBounds(juce::Rectangle<int> newBounds);

private:
    DebugLog() = default;
    ~DebugLog();

    void logMessage(const juce::String& message, juce::Colour colour);

    template <typename T>
    static juce::String toString(const T& val)
    {
        std::stringstream ss;
        ss << val;
        return juce::String(ss.str());
    }

    template <typename... Args>
    juce::String buildString(Args... args)
    {
        juce::StringArray parts;
        (parts.add(toString(args)), ...);
        return parts.joinIntoString(" ");
    }

public:
    struct PrintObject
    {
        DebugLog& owner;

        template<typename... Args>
        void operator()(Args... args)
        {
            owner.logMessage(owner.buildString(args...), juce::Colours::white);
        }

        template<typename... Args> void red(Args... args)    { owner.logMessage(owner.buildString(args...), juce::Colours::red); }
        template<typename... Args> void green(Args... args)  { owner.logMessage(owner.buildString(args...), juce::Colours::green); }
        template<typename... Args> void blue(Args... args)   { owner.logMessage(owner.buildString(args...), juce::Colours::blue); }
        template<typename... Args> void yellow(Args... args){ owner.logMessage(owner.buildString(args...), juce::Colours::yellow); }
        template<typename... Args> void magenta(Args... args){ owner.logMessage(owner.buildString(args...), juce::Colours::magenta); }
        template<typename... Args> void cyan(Args... args)   { owner.logMessage(owner.buildString(args...), juce::Colours::cyan); }
    };

    PrintObject print {*this};

private:
    std::unique_ptr<DebugConsole> window;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugLog)
};

extern DebugLog& debug;