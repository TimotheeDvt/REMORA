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
} // namespace

GraphNodeComponent::GraphNodeComponent(GraphEditorComponent& editorIn, Graph::NodeId nodeIdIn,
                                        const Graph::NodeTypeInfo& typeInfoIn, std::vector<float> paramsIn)
    : editor(editorIn), nodeId(nodeIdIn), typeInfo(typeInfoIn), params(std::move(paramsIn)) {
    for (int i = 0; i < typeInfo.numInputs; ++i) {
        auto* pin = inputPins.add(new GraphPinComponent(editor, nodeId, i, false));
        addAndMakeVisible(pin);
    }
    for (int i = 0; i < typeInfo.numOutputs; ++i) {
        auto* pin = outputPins.add(new GraphPinComponent(editor, nodeId, i, true));
        addAndMakeVisible(pin);
    }

    const bool editable = editor.isGraphEditable();
    for (size_t i = 0; i < params.size(); ++i) {
        auto* label = paramNameLabels.add(new juce::Label({}, paramLabelFor(i)));
        label->setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
        label->setColour(juce::Label::textColourId, Palette::textMid);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);

        auto* box = paramEditors.add(new juce::TextEditor());
        box->setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
        box->setJustification(juce::Justification::centredRight);
        box->setText(juce::String(params[i], 3), false);
        box->setReadOnly(!editable);
        box->setSelectAllWhenFocused(true);
        const int index = static_cast<int>(i);
        box->onFocusLost = [this, index, box] {
            const float value = box->getText().getFloatValue();
            params[static_cast<size_t>(index)] = value;
            editor.updateNodeParam(nodeId, index, value);
        };
        box->onReturnKey = [box] { box->giveAwayKeyboardFocus(); };
        addAndMakeVisible(box);
    }

    if (hasValueSlider(typeInfo)) {
        const float initialValue = params[0];
        float lo = juce::jmin(initialValue * 0.5f, initialValue * 2.0f);
        float hi = juce::jmax(initialValue * 0.5f, initialValue * 2.0f);
        if (juce::approximatelyEqual(lo, hi)) {
            lo = -1.0f;
            hi = 1.0f;
        }

        valueSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        valueSlider->setRange(lo, hi);
        valueSlider->setValue(initialValue, juce::dontSendNotification);
        valueSlider->setChangeNotificationOnlyOnRelease(false);
        valueSlider->setEnabled(editable);
        auto* box = paramEditors.getFirst();
        valueSlider->onValueChange = [this, box] {
            const float sliderValue = static_cast<float>(valueSlider->getValue());
            params[0] = sliderValue;
            box->setText(juce::String(sliderValue, 3), false);
            editor.updateNodeParam(nodeId, 0, sliderValue);
        };
        box->onFocusLost = [this, box] {
            const float typedValue = box->getText().getFloatValue();
            params[0] = typedValue;
            valueSlider->setValue(typedValue, juce::dontSendNotification);
            editor.updateNodeParam(nodeId, 0, typedValue);
        };
        addAndMakeVisible(*valueSlider);
    }

    setSize(kWidth, preferredHeight(typeInfo));
}

juce::String GraphNodeComponent::paramLabelFor(size_t index) const {
    if (index < typeInfo.paramNames.size())
        return typeInfo.paramNames[index];
    return "v" + juce::String(static_cast<int>(index));
}

bool GraphNodeComponent::hasValueSlider(const Graph::NodeTypeInfo& typeInfo) {
    return typeInfo.id == "math.constant" && !typeInfo.defaultParams.empty();
}

int GraphNodeComponent::paramsHeight(const Graph::NodeTypeInfo& typeInfo) {
    return static_cast<int>(typeInfo.defaultParams.size()) * kParamRowHeight
        + (hasValueSlider(typeInfo) ? kSliderRowHeight : 0);
}

int GraphNodeComponent::preferredHeight(const Graph::NodeTypeInfo& typeInfo) {
    const int rows = juce::jmax(1, typeInfo.numInputs, typeInfo.numOutputs);
    return kHeaderHeight + paramsHeight(typeInfo) + rows * kRowHeight + 6;
}

int GraphNodeComponent::portsTop() const noexcept {
    return kHeaderHeight + static_cast<int>(params.size()) * kParamRowHeight
        + (valueSlider != nullptr ? kSliderRowHeight : 0);
}

void GraphNodeComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(Palette::panel);
    g.fillRoundedRectangle(bounds, 4.0f);

    auto header = bounds.removeFromTop(static_cast<float>(kHeaderHeight));
    g.setColour(categoryColour(typeInfo.category));
    g.fillRoundedRectangle(header, 4.0f);
    g.fillRect(header.withTop(header.getBottom() - 4.0f)); // square off the bottom corners of the header

    auto infoArea = header.removeFromRight(static_cast<float>(kHeaderHeight));

    g.setColour(Palette::textHi);
    g.setFont(12.0f);
    g.drawText(typeInfo.displayName, header.reduced(6.0f, 0.0f), juce::Justification::centred, true);

    {
        auto circle = infoArea.withSizeKeepingCentre(13.0f, 13.0f);
        g.setColour(Palette::textHi.withAlpha(0.85f));
        g.drawEllipse(circle, 1.2f);
        g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyle("Bold")));
        g.drawText("?", circle, juce::Justification::centred, false);
    }

    g.setColour(Palette::border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);

    const int top = portsTop();
    const int halfWidth = kWidth / 2 - kPinSize - 4;
    g.setFont(10.5f);
    g.setColour(Palette::textMid);
    for (int i = 0; i < typeInfo.numInputs; ++i) {
        const int y = top + i * kRowHeight;
        const juce::String label = typeInfo.inputNames.size() > static_cast<size_t>(i) ? typeInfo.inputNames[static_cast<size_t>(i)] : juce::String();
        g.drawText(label, kPinSize + 4, y, halfWidth, kRowHeight, juce::Justification::centredLeft, true);
    }
    for (int i = 0; i < typeInfo.numOutputs; ++i) {
        const int y = top + i * kRowHeight;
        const juce::String label = typeInfo.outputNames.size() > static_cast<size_t>(i) ? typeInfo.outputNames[static_cast<size_t>(i)] : juce::String();
        g.drawText(label, kWidth / 2, y, halfWidth, kRowHeight, juce::Justification::centredRight, true);
    }
}

void GraphNodeComponent::resized() {
    infoButtonBounds = { kWidth - kHeaderHeight, 0, kHeaderHeight, kHeaderHeight };

    for (int i = 0; i < paramEditors.size(); ++i) {
        const int y = kHeaderHeight + i * kParamRowHeight;
        const int nameWidth = kWidth * 2 / 5;
        paramNameLabels.getUnchecked(i)->setBounds(4, y, nameWidth - 4, kParamRowHeight);
        paramEditors.getUnchecked(i)->setBounds(nameWidth, y, kWidth - nameWidth - 4, kParamRowHeight - 2);
    }

    if (valueSlider != nullptr) {
        const int y = kHeaderHeight + static_cast<int>(paramEditors.size()) * kParamRowHeight;
        valueSlider->setBounds(4, y, kWidth - 8, kSliderRowHeight - 2);
    }

    const int top = portsTop();
    for (int i = 0; i < inputPins.size(); ++i) {
        const int y = top + i * kRowHeight + kRowHeight / 2 - kPinSize / 2;
        inputPins.getUnchecked(i)->setBounds(0, y, kPinSize, kPinSize);
    }
    for (int i = 0; i < outputPins.size(); ++i) {
        const int y = top + i * kRowHeight + kRowHeight / 2 - kPinSize / 2;
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
    if (!e.mods.isPopupMenu() && !e.mods.isCtrlDown() && infoButtonBounds.contains(e.getPosition())) {
        showInfoPopup();
        return;
    }
    if (e.mods.isCtrlDown()) {
        editor.handleCanvasMouseDown(e.getEventRelativeTo(&editor));
        return;
    }
    if (e.mods.isPopupMenu()) {
        editor.showNodeContextMenu(nodeId);
        return;
    }
    dragStartPos = getPosition();
}

void GraphNodeComponent::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isCtrlDown()) {
        editor.handleCanvasMouseDrag(e.getEventRelativeTo(&editor));
        return;
    }
    if (e.mods.isPopupMenu())
        return;
    const auto newPos = dragStartPos + e.getOffsetFromDragStart();
    setTopLeftPosition(newPos);
    editor.nodeMoved(nodeId, static_cast<float>(newPos.x), static_cast<float>(newPos.y));
}

void GraphNodeComponent::mouseUp(const juce::MouseEvent&) {
    editor.handleCanvasMouseUp();
}

void GraphNodeComponent::showInfoPopup() {
    juce::String text;
    text << typeInfo.displayName;
    if (typeInfo.subcategory.isNotEmpty())
        text << "  (" << typeInfo.subcategory << ")";
    text << "\n\n";

    if (typeInfo.numInputs > 0) {
        text << "Inputs:\n";
        for (int i = 0; i < typeInfo.numInputs; ++i) {
            const juce::String n = typeInfo.inputNames.size() > static_cast<size_t>(i)
                ? typeInfo.inputNames[static_cast<size_t>(i)] : ("in" + juce::String(i));
            text << "  - " << n << "\n";
        }
        text << "\n";
    }

    text << "Outputs:\n";
    for (int i = 0; i < typeInfo.numOutputs; ++i) {
        const juce::String n = typeInfo.outputNames.size() > static_cast<size_t>(i)
            ? typeInfo.outputNames[static_cast<size_t>(i)]
            : (typeInfo.numOutputs > 1 ? ("out" + juce::String(i)) : juce::String("out"));
        text << "  - " << n << "\n";
    }

    if (!params.empty()) {
        text << "\nParameters:\n";
        for (size_t i = 0; i < params.size(); ++i)
            text << "  - " << paramLabelFor(i) << "\n";
    }

    if (typeInfo.description.isNotEmpty())
        text << "\n" << typeInfo.description;

    auto content = std::make_unique<juce::TextEditor>();
    content->setMultiLine(true, true);
    content->setReadOnly(true);
    content->setCaretVisible(false);
    content->setScrollbarsShown(true);
    content->setColour(juce::TextEditor::backgroundColourId, Palette::panel);
    content->setColour(juce::TextEditor::textColourId, Palette::textHi);
    content->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    content->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    content->setFont(juce::Font(juce::FontOptions().withHeight(12.5f)));
    content->setText(text, false);
    content->setSize(280, 240);

    juce::CallOutBox::launchAsynchronously(std::move(content), localAreaToGlobal(infoButtonBounds), nullptr);
}
