#include "DebugConsole.h"
#include "DebugLog.h"
#include "StyleHelpers.h"

class DebugConsole::Content   : public juce::Component,
                                private juce::ListBoxModel
{
public:
    Content()
    {
        addAndMakeVisible (listBox);
        listBox.setModel (this);
        listBox.setColour (juce::ListBox::backgroundColourId, getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker(0.8f));

        addAndMakeVisible (clearButton);
        styleButton(clearButton, "Clear", {}, [this] { clear(); });
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        clearButton.setBounds (bounds.removeFromBottom (24).reduced (4));
        listBox.setBounds (bounds);

        currentFontSize = juce::jmap ((float) getHeight(), 150.0f, 800.0f, 10.0f, 18.0f);
        listBox.setRowHeight (static_cast<int> (currentFontSize + 2.0f));
    }

    void addMessage (const juce::String& message, juce::Colour colour)
    {
        lines.add ({ message, colour, nextLineNumber++ });

        if (lines.size() > maxLines)
            lines.removeRange (0, 3);

        listBox.updateContent();
        listBox.scrollToEnsureRowIsOnscreen (lines.size() - 1);
    }

    void clear()
    {
        lines.clear();
        nextLineNumber = 0;
        listBox.updateContent();
    }

    int getNumRows() override
    {
        return lines.size();
    }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool /*rowIsSelected*/) override
    {
        if (juce::isPositiveAndBelow (rowNumber, lines.size()))
        {
            const auto& line = lines[rowNumber];
            g.setColour (line.colour);
            g.setFont (juce::Font (juce::FontOptions().withName(juce::Font::getDefaultMonospacedFontName()).withHeight(currentFontSize)));
            g.drawText (juce::String (line.lineNumber) + ": " + line.message, juce::Rectangle<int> (width, height).withX (4), juce::Justification::centredLeft, true);
        }
    }

private:
    struct LogLine
    {
        juce::String message;
        juce::Colour colour;
        int lineNumber;
    };

    juce::ListBox listBox;
    juce::TextButton clearButton;
    juce::Array<LogLine> lines;
    int nextLineNumber = 0;
    float currentFontSize = 12.0f;

    static constexpr int maxLines = 200;
};

DebugConsole::DebugConsole(const juce::String& name)
    : DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel()
                               .findColour(juce::ResizableWindow::backgroundColourId),
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);

    contentComponent = new Content();
    setContentOwned (contentComponent, false);

    setResizable(true, true);
    setResizeLimits(300, 200, 1000, 800);
    centreWithSize(500, 400);
}

DebugConsole::~DebugConsole()
{
}

void DebugConsole::closeButtonPressed()
{
    DebugLog::getInstance().hide();
}

void DebugConsole::addMessage(const juce::String& message, juce::Colour colour)
{
    if (contentComponent != nullptr)
        contentComponent->addMessage(message, colour);
}

void DebugConsole::clear()
{
    if (contentComponent != nullptr)
        contentComponent->clear();
}