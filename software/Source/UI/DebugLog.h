#pragma once

#include <JuceHeader.h>
#include <sstream>

// Forward declaration
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

    // The actual print implementation
    void logMessage(const juce::String& message, juce::Colour colour);

    // Helper to convert anything to string
    template <typename T>
    static juce::String toString(const T& val)
    {
        std::stringstream ss;
        ss << val;
        return juce::String(ss.str());
    }

    // Variadic template to build the string
    template <typename... Args>
    juce::String buildString(Args... args)
    {
        juce::StringArray parts;
        (parts.add(toString(args)), ...);
        return parts.joinIntoString(" ");
    }

public:
    // The object that provides the print API
    struct PrintObject
    {
        DebugLog& owner;

        // Default print (white text)
        template<typename... Args>
        void operator()(Args... args)
        {
            owner.logMessage(owner.buildString(args...), juce::Colours::white);
        }

        // Coloured print methods
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

// Global accessor
extern DebugLog& debug;