#include "GraphEditorComponent.h"
#include "Palette.h"

GraphEditorComponent::GraphEditorComponent(REMORAProcessor& p) : processor(p) {
}

void GraphEditorComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::bg);
    g.setColour(Palette::textMid);
    g.setFont(16.0f);
    g.drawText("Graph editor - coming soon", getLocalBounds(), juce::Justification::centred, false);
}
