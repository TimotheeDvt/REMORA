#pragma once

#include <JuceHeader.h>

class GraphEditorComponent;

// The pannable/zoomable surface GraphEditorComponent applies its AffineTransform to (not to itself).
class GraphCanvasComponent : public juce::Component {
public:
    explicit GraphCanvasComponent(GraphEditorComponent& editorIn) : editor(editorIn) {}

    void paint(juce::Graphics& g) override;
    // Panning can put node positions outside this component's nominal size; the default hitTest would block them.
    bool hitTest(int, int) override { return true; }

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    GraphEditorComponent& editor;
};
