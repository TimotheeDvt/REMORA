#include "GraphPinComponent.h"
#include "GraphEditorComponent.h"
#include "Palette.h"

GraphPinComponent::GraphPinComponent(GraphEditorComponent& editorIn, Graph::NodeId nodeIdIn, int portIn, bool isOutput)
    : editor(editorIn), nodeId(nodeIdIn), port(portIn), output(isOutput) {
    setRepaintsOnMouseActivity(true);
}

void GraphPinComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(output ? Palette::green : Palette::accent);
    g.fillEllipse(bounds);
    g.setColour(isMouseOver() ? Palette::textHi : Palette::border);
    g.drawEllipse(bounds, 1.5f);
}

juce::String GraphPinComponent::getTooltip() {
    if (!output || !editor.isGraphEditable())
        return {};
    return juce::String(editor.liveOutputValue(nodeId, port), 4);
}

void GraphPinComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isCtrlDown())
        editor.handleCanvasMouseDown(e.getEventRelativeTo(&editor));
    else if (e.mods.isPopupMenu())
        editor.showPinContextMenu(*this);
    else
        editor.handlePinMouseDown(*this, e.getEventRelativeTo(&editor));
}

void GraphPinComponent::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isCtrlDown())
        editor.handleCanvasMouseDrag(e.getEventRelativeTo(&editor));
    else if (!e.mods.isPopupMenu())
        editor.handlePinMouseDrag(e.getEventRelativeTo(&editor));
}

void GraphPinComponent::mouseUp(const juce::MouseEvent& e) {
    editor.handleCanvasMouseUp();
    if (!e.mods.isCtrlDown() && !e.mods.isPopupMenu())
        editor.handlePinMouseUp(e.getEventRelativeTo(&editor));
}
