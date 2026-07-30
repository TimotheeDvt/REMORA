#include "GraphNodeComponent.h"
#include "GraphEditorComponent.h"
#include "Palette.h"

namespace {
juce::Colour categoryColour(Graph::NodeCategory category) {
    switch (category) {
        case Graph::NodeCategory::Source: return Palette::green;
        case Graph::NodeCategory::Sink:   return Palette::yellow;
        case Graph::NodeCategory::Math:
        default:                          return Palette::accent;
    }
}
}

GraphNodeComponent::GraphNodeComponent(GraphEditorComponent& editorIn, Graph::NodeId nodeIdIn, const Graph::NodeTypeInfo& typeInfoIn)
    : editor(editorIn), nodeId(nodeIdIn), typeInfo(typeInfoIn) {
    for (int i = 0; i < typeInfo.numInputs; ++i) {
        auto* pin = inputPins.add(new GraphPinComponent(editor, nodeId, i, false));
        addAndMakeVisible(pin);
    }
    for (int i = 0; i < typeInfo.numOutputs; ++i) {
        auto* pin = outputPins.add(new GraphPinComponent(editor, nodeId, i, true));
        addAndMakeVisible(pin);
    }
    setSize(kWidth, preferredHeight(typeInfo));
}

int GraphNodeComponent::preferredHeight(const Graph::NodeTypeInfo& typeInfo) {
    const int rows = juce::jmax(1, typeInfo.numInputs, typeInfo.numOutputs);
    return kHeaderHeight + rows * kRowHeight + 6;
}

void GraphNodeComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(Palette::panel);
    g.fillRoundedRectangle(bounds, 4.0f);

    auto header = bounds.removeFromTop(static_cast<float>(kHeaderHeight));
    g.setColour(categoryColour(typeInfo.category));
    g.fillRoundedRectangle(header, 4.0f);
    g.fillRect(header.withTop(header.getBottom() - 4.0f)); // square off the bottom corners of the header

    g.setColour(Palette::textHi);
    g.setFont(12.0f);
    g.drawText(typeInfo.displayName, header.reduced(6.0f, 0.0f), juce::Justification::centred, true);

    g.setColour(Palette::border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);

    g.setFont(10.5f);
    g.setColour(Palette::textMid);
    for (int i = 0; i < typeInfo.numInputs; ++i) {
        auto y = kHeaderHeight + i * kRowHeight;
        g.drawText(typeInfo.inputNames.size() > static_cast<size_t>(i) ? typeInfo.inputNames[static_cast<size_t>(i)] : juce::String(),
                   kPinSize + 4, y, kWidth - (kPinSize + 4) * 2, kRowHeight, juce::Justification::centredLeft, true);
    }
}

void GraphNodeComponent::resized() {
    for (int i = 0; i < inputPins.size(); ++i) {
        const int y = kHeaderHeight + i * kRowHeight + kRowHeight / 2 - kPinSize / 2;
        inputPins.getUnchecked(i)->setBounds(0, y, kPinSize, kPinSize);
    }
    for (int i = 0; i < outputPins.size(); ++i) {
        const int y = kHeaderHeight + i * kRowHeight + kRowHeight / 2 - kPinSize / 2;
        outputPins.getUnchecked(i)->setBounds(getWidth() - kPinSize, y, kPinSize, kPinSize);
    }
}

juce::Point<int> GraphNodeComponent::getInputPinCentre(int port) const {
    if (auto* pin = inputPins[port])
        return getBounds().getPosition() + pin->getBounds().getCentre();
    return getBounds().getCentre();
}

juce::Point<int> GraphNodeComponent::getOutputPinCentre(int port) const {
    if (auto* pin = outputPins[port])
        return getBounds().getPosition() + pin->getBounds().getCentre();
    return getBounds().getCentre();
}

void GraphNodeComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        editor.showNodeContextMenu(nodeId);
        return;
    }
    dragStartPos = getPosition();
}

void GraphNodeComponent::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;
    const auto newPos = dragStartPos + e.getOffsetFromDragStart();
    setTopLeftPosition(newPos);
    editor.nodeMoved(nodeId, static_cast<float>(newPos.x), static_cast<float>(newPos.y));
}
