#pragma once

#include <JuceHeader.h>

class GraphEditorComponent;

// The pannable/zoomable surface: GraphEditorComponent applies an
// AffineTransform to this (not to itself), so node dragging and pin
// hit-testing - both driven by JUCE's own transform-aware mouse coordinate
// mapping - keep working unmodified regardless of zoom level. hitTest always
// returns true because panning can put node positions outside this
// component's own nominal size, and the default hitTest would otherwise
// block JUCE from even checking children out there.
class GraphCanvasComponent : public juce::Component {
public:
    explicit GraphCanvasComponent(GraphEditorComponent& editorIn) : editor(editorIn) {}

    void paint(juce::Graphics& g) override;
    bool hitTest(int, int) override { return true; }

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    GraphEditorComponent& editor;
};
