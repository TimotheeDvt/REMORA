#include "GraphCanvasComponent.h"
#include "GraphEditorComponent.h"
#include "Palette.h"

void GraphCanvasComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::bg);
    editor.paintConnections(g);
}

void GraphCanvasComponent::mouseDown(const juce::MouseEvent& e) {
    editor.handleBackgroundMouseDown(e.getEventRelativeTo(&editor));
}

void GraphCanvasComponent::mouseDrag(const juce::MouseEvent& e) {
    editor.handleBackgroundMouseDrag(e.getEventRelativeTo(&editor));
}

void GraphCanvasComponent::mouseUp(const juce::MouseEvent&) {
    editor.handleCanvasMouseUp();
}
