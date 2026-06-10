#include "DebugLog.h"
#include "DebugConsole.h"

// Define the global accessor
DebugLog& debug = DebugLog::getInstance();

DebugLog& DebugLog::getInstance()
{
    static DebugLog instance;
    return instance;
}

DebugLog::~DebugLog()
{
    // The unique_ptr will automatically delete the window
}

void DebugLog::show()
{
    if (!window)
    {
        window = std::make_unique<DebugConsole>("DEBUG");
    }
    window->setVisible(true);
    window->toFront(true);
    sendChangeMessage();
}

void DebugLog::hide()
{
    if (window)
    {
        window->setVisible(false);
        sendChangeMessage();
    }
}

bool DebugLog::isWindowOpen() const
{
    return window && window->isVisible();
}

void DebugLog::logMessage(const juce::String& message, juce::Colour colour)
{
    if (!isWindowOpen())
        return;

    // We must post the UI update to the message thread
    juce::MessageManager::callAsync([this, message, colour]()
    {
        if (window && window->isVisible()) // Check again in case it was closed
        {
            window->addMessage(message, colour);
        }
    });
}