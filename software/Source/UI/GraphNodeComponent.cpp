#include "GraphNodeComponent.h"
#include "GraphEditorComponent.h"
#include "Palette.h"

namespace {
constexpr double kHighlightDurationMs = 1400.0;
constexpr double kHighlightBlinkHz = 3.0;

juce::Colour categoryColour(Graph::NodeCategory category) {
    switch (category) {
        case Graph::NodeCategory::Source:  return Palette::green;
        case Graph::NodeCategory::Sink:    return Palette::yellow;
        case Graph::NodeCategory::Display: return Palette::purple;
        case Graph::NodeCategory::Math:
        default:                           return Palette::accent;
    }
}
} // namespace

GraphNodeComponent::GraphNodeComponent(GraphEditorComponent& editorIn, Graph::NodeId nodeIdIn,
                                        const Graph::NodeTypeInfo& typeInfoIn, std::vector<float> paramsIn,
                                        float initialW, float initialH, juce::String labelIn)
    : editor(editorIn), nodeId(nodeIdIn), typeInfo(typeInfoIn), params(std::move(paramsIn)), nodeLabel(std::move(labelIn)) {
    for (int i = 0; i < typeInfo.numInputs; ++i) {
        auto* pin = inputPins.add(new GraphPinComponent(editor, nodeId, i, false));
        addAndMakeVisible(pin);
    }
    for (int i = 0; i < typeInfo.numOutputs; ++i) {
        auto* pin = outputPins.add(new GraphPinComponent(editor, nodeId, i, true));
        addAndMakeVisible(pin);
    }

    setWantsKeyboardFocus(true);

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

    if (typeInfo.displayKind != Graph::DisplayKind::None) {
        const int w = initialW > 0.0f ? static_cast<int>(initialW) : static_cast<int>(typeInfo.displayDefaultWidth);
        const int h = initialH > 0.0f ? static_cast<int>(initialH) : static_cast<int>(typeInfo.displayDefaultHeight);
        setSize(juce::jmax(kMinDisplayWidth, w), juce::jmax(kMinDisplayHeight, h));
        startTimerHz(30); // keeps the live display current even without a highlight in progress
    } else {
        setSize(kWidth, preferredHeight(typeInfo, params.size()));
    }
}

juce::String GraphNodeComponent::paramLabelFor(size_t index) const {
    if (index < typeInfo.paramNames.size())
        return typeInfo.paramNames[index];
    return "v" + juce::String(static_cast<int>(index));
}

bool GraphNodeComponent::hasValueSlider(const Graph::NodeTypeInfo& typeInfo) {
    return typeInfo.id == "math.constant" && !typeInfo.defaultParams.empty();
}

int GraphNodeComponent::paramsHeight(const Graph::NodeTypeInfo& typeInfo, size_t paramCount) {
    return static_cast<int>(paramCount) * kParamRowHeight
        + (hasValueSlider(typeInfo) ? kSliderRowHeight : 0);
}

int GraphNodeComponent::preferredHeight(const Graph::NodeTypeInfo& typeInfo, size_t paramCount) {
    const int rows = juce::jmax(1, typeInfo.numInputs, typeInfo.numOutputs);
    return kHeaderHeight + paramsHeight(typeInfo, paramCount) + rows * kRowHeight + 6;
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
    g.drawText(displayCaption(), header.reduced(6.0f, 0.0f), juce::Justification::centred, true);

    {
        auto circle = infoArea.withSizeKeepingCentre(13.0f, 13.0f);
        g.setColour(Palette::textHi.withAlpha(0.85f));
        g.drawEllipse(circle, 1.2f);
        g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyle("Bold")));
        g.drawText("?", circle, juce::Justification::centred, false);
    }

    g.setColour(Palette::border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);

    if (editor.isNodeSelected(nodeId)) {
        g.setColour(Palette::accent);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 2.0f);
    }

    if (highlightActive) {
        const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - highlightStartMs;
        const float envelope = juce::jlimit(0.0f, 1.0f, 1.0f - static_cast<float>(elapsedMs / kHighlightDurationMs));
        const float blink = 0.5f - 0.5f * std::cos(elapsedMs * (juce::MathConstants<double>::twoPi * kHighlightBlinkHz / 1000.0));
        const float alpha = envelope * static_cast<float>(blink);
        if (alpha > 0.01f) {
            g.setColour(Palette::yellow.withAlpha(alpha));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.5f), 5.0f, 2.0f + 3.0f * alpha);
        }
    }

    const int top = portsTop();

    if (typeInfo.displayKind != Graph::DisplayKind::None) {
        paintDisplay(g, { kPinSize + 2, top, getWidth() - (kPinSize + 2) * 2, getHeight() - top - 2 });

        auto grip = juce::Rectangle<float>(static_cast<float>(getWidth() - kResizeGripSize),
                                            static_cast<float>(getHeight() - kResizeGripSize),
                                            static_cast<float>(kResizeGripSize), static_cast<float>(kResizeGripSize));
        g.setColour(Palette::textLo);
        for (int i = 1; i <= 3; ++i) {
            const float o = static_cast<float>(i) * 3.5f;
            g.drawLine(grip.getRight() - o, grip.getBottom(), grip.getRight(), grip.getBottom() - o, 1.2f);
        }
        return;
    }

    const int halfWidth = getWidth() / 2 - kPinSize - 4;
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
        g.drawText(label, getWidth() / 2, y, halfWidth, kRowHeight, juce::Justification::centredRight, true);
    }
}

void GraphNodeComponent::paintDisplay(juce::Graphics& g, juce::Rectangle<int> area) {
    const float liveValue = editor.liveOutputValue(nodeId, 0);
    const float lo = params.size() > 0 ? params[0] : 0.0f;
    const float hi = params.size() > 1 ? params[1] : 1.0f;
    auto floatArea = area.toFloat();

    switch (typeInfo.displayKind) {
        case Graph::DisplayKind::Number: {
            g.setColour(Palette::textHi);
            const float fontHeight = juce::jlimit(14.0f, 40.0f, floatArea.getHeight() * 0.4f);
            g.setFont(juce::Font(juce::FontOptions().withHeight(fontHeight).withStyle("Bold")));
            g.drawText(juce::String(liveValue, 3), area, juce::Justification::centred, false);
            break;
        }
        case Graph::DisplayKind::Meter: {
            auto barArea = floatArea.reduced(2.0f);
            auto labelArea = barArea.removeFromBottom(14.0f);
            g.setColour(Palette::bg);
            g.fillRoundedRectangle(barArea, 3.0f);
            const float t = hi > lo ? juce::jlimit(0.0f, 1.0f, (liveValue - lo) / (hi - lo)) : 0.0f;
            if (t > 0.0f) {
                auto fill = barArea.removeFromBottom(barArea.getHeight() * t);
                g.setColour(Palette::accent);
                g.fillRoundedRectangle(fill, 3.0f);
            }
            g.setColour(Palette::border);
            g.drawRoundedRectangle(floatArea.reduced(2.0f).withTrimmedBottom(14.0f), 3.0f, 1.0f);
            g.setColour(Palette::textMid);
            g.setFont(10.0f);
            g.drawText(juce::String(liveValue, 2), labelArea.toNearestInt(), juce::Justification::centred, false);
            break;
        }
        case Graph::DisplayKind::Scope: {
            g.setColour(Palette::bg);
            g.fillRoundedRectangle(floatArea, 3.0f);
            g.setColour(Palette::border);
            g.drawRoundedRectangle(floatArea.reduced(0.5f), 3.0f, 1.0f);
            if (scopeHistory.size() > 1) {
                juce::Path p;
                const auto plotArea = floatArea.reduced(2.0f);
                const float n = static_cast<float>(scopeHistory.size() - 1);
                for (size_t i = 0; i < scopeHistory.size(); ++i) {
                    const float t = hi > lo ? juce::jlimit(0.0f, 1.0f, (scopeHistory[i] - lo) / (hi - lo)) : 0.0f;
                    const float x = plotArea.getX() + plotArea.getWidth() * (static_cast<float>(i) / n);
                    const float y = plotArea.getBottom() - t * plotArea.getHeight();
                    if (i == 0) p.startNewSubPath(x, y);
                    else p.lineTo(x, y);
                }
                g.setColour(Palette::accent);
                g.strokePath(p, juce::PathStrokeType(1.5f));
            }
            break;
        }
        case Graph::DisplayKind::None:
            break;
    }
}

void GraphNodeComponent::resized() {
    infoButtonBounds = { getWidth() - kHeaderHeight, 0, kHeaderHeight, kHeaderHeight };

    for (int i = 0; i < paramEditors.size(); ++i) {
        const int y = kHeaderHeight + i * kParamRowHeight;
        const int nameWidth = getWidth() * 2 / 5;
        paramNameLabels.getUnchecked(i)->setBounds(4, y, nameWidth - 4, kParamRowHeight);
        paramEditors.getUnchecked(i)->setBounds(nameWidth, y, getWidth() - nameWidth - 4, kParamRowHeight - 2);
    }

    if (valueSlider != nullptr) {
        const int y = kHeaderHeight + static_cast<int>(paramEditors.size()) * kParamRowHeight;
        valueSlider->setBounds(4, y, getWidth() - 8, kSliderRowHeight - 2);
    }

    const int top = portsTop();
    if (typeInfo.displayKind != Graph::DisplayKind::None) {
        // Single in/out, centred in the (resizable) display body rather than pinned to the top row.
        const int centreY = (top + getHeight()) / 2;
        for (int i = 0; i < inputPins.size(); ++i)
            inputPins.getUnchecked(i)->setBounds(0, centreY - kPinSize / 2, kPinSize, kPinSize);
        for (int i = 0; i < outputPins.size(); ++i)
            outputPins.getUnchecked(i)->setBounds(getWidth() - kPinSize, centreY - kPinSize / 2, kPinSize, kPinSize);
        return;
    }
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
    grabKeyboardFocus();
    if (!e.mods.isPopupMenu() && !e.mods.isCtrlDown() && infoButtonBounds.contains(e.getPosition())) {
        showInfoPopup();
        return;
    }
    if (typeInfo.displayKind != Graph::DisplayKind::None && !e.mods.isPopupMenu() && !e.mods.isCtrlDown()) {
        const juce::Rectangle<int> grip(getWidth() - kResizeGripSize, getHeight() - kResizeGripSize,
                                         kResizeGripSize, kResizeGripSize);
        if (grip.contains(e.getPosition())) {
            isResizing = true;
            resizeStartSize = { getWidth(), getHeight() };
            return;
        }
    }
    if (e.mods.isCtrlDown()) {
        editor.handleCanvasMouseDown(e.getEventRelativeTo(&editor));
        return;
    }
    if (e.mods.isPopupMenu()) {
        if (!editor.isNodeSelected(nodeId))
            editor.selectNode(nodeId, false);
        editor.showNodeContextMenu(nodeId);
        return;
    }
    if (e.mods.isShiftDown())
        editor.selectNode(nodeId, true);
    else if (!editor.isNodeSelected(nodeId))
        editor.selectNode(nodeId, false);
    editor.beginGroupDrag();
}

void GraphNodeComponent::mouseDrag(const juce::MouseEvent& e) {
    if (isResizing) {
        const auto delta = e.getOffsetFromDragStart();
        const int newW = juce::jmax(kMinDisplayWidth, resizeStartSize.x + delta.x);
        const int newH = juce::jmax(kMinDisplayHeight, resizeStartSize.y + delta.y);
        setSize(newW, newH);
        editor.nodeResized(nodeId, static_cast<float>(newW), static_cast<float>(newH));
        return;
    }
    if (e.mods.isCtrlDown()) {
        editor.handleCanvasMouseDrag(e.getEventRelativeTo(&editor));
        return;
    }
    if (e.mods.isPopupMenu())
        return;
    editor.dragSelectedNodesBy(e.getOffsetFromDragStart());
}

void GraphNodeComponent::mouseUp(const juce::MouseEvent&) {
    if (isResizing) {
        isResizing = false;
        return;
    }
    editor.handleCanvasMouseUp();
}

void GraphNodeComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!editor.isGraphEditable() || e.y >= kHeaderHeight || infoButtonBounds.contains(e.getPosition()))
        return;
    beginLabelEdit();
}

void GraphNodeComponent::beginLabelEdit() {
    if (labelEditor != nullptr)
        return;

    auto* box = new juce::TextEditor();
    labelEditor.reset(box);
    box->setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    box->setJustification(juce::Justification::centred);
    box->setColour(juce::TextEditor::backgroundColourId, Palette::panel);
    box->setColour(juce::TextEditor::textColourId, Palette::textHi);
    box->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    box->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    box->setSelectAllWhenFocused(true);
    box->setText(displayCaption(), false);
    box->onReturnKey = [box] { box->giveAwayKeyboardFocus(); };
    box->onFocusLost = [this] { commitLabelEdit(); };
    addAndMakeVisible(box);
    box->setBounds(4, 0, juce::jmax(10, infoButtonBounds.getX() - 4), kHeaderHeight);
    box->grabKeyboardFocus();
    box->selectAll();
}

void GraphNodeComponent::commitLabelEdit() {
    if (labelEditor == nullptr)
        return;
    const juce::String typed = labelEditor->getText().trim();
    labelEditor.reset();
    if (typed != nodeLabel) {
        nodeLabel = typed;
        editor.updateNodeLabel(nodeId, nodeLabel);
    }
    repaint();
}

void GraphNodeComponent::startHighlight() {
    highlightStartMs = juce::Time::getMillisecondCounterHiRes();
    highlightActive = true;
    startTimerHz(30);
    repaint();
}

void GraphNodeComponent::timerCallback() {
    if (typeInfo.displayKind == Graph::DisplayKind::Scope) {
        constexpr size_t kMaxScopeSamples = 120; // ~4s at the 30Hz tick rate below
        scopeHistory.push_back(editor.liveOutputValue(nodeId, 0));
        if (scopeHistory.size() > kMaxScopeSamples)
            scopeHistory.erase(scopeHistory.begin());
    }

    if (highlightActive && juce::Time::getMillisecondCounterHiRes() - highlightStartMs >= kHighlightDurationMs)
        highlightActive = false;

    repaint();

    if (!highlightActive && typeInfo.displayKind == Graph::DisplayKind::None)
        stopTimer();
}

juce::String GraphNodeComponent::getTooltip() {
    if (!editor.isGraphEditable())
        return {};

    juce::String text = displayCaption();
    if (typeInfo.category == Graph::NodeCategory::Sink) {
        // Sinks have no output - show the value currently flowing into their (single) input instead.
        for (int i = 0; i < typeInfo.numInputs; ++i) {
            const juce::String label = typeInfo.inputNames.size() > static_cast<size_t>(i)
                ? typeInfo.inputNames[static_cast<size_t>(i)] : ("in" + juce::String(i));
            text << "\n" << label << ": " << juce::String(editor.liveOutputValue(nodeId, i), 4);
        }
        return text;
    }
    for (int i = 0; i < typeInfo.numOutputs; ++i) {
        const juce::String label = typeInfo.outputNames.size() > static_cast<size_t>(i)
            ? typeInfo.outputNames[static_cast<size_t>(i)]
            : (typeInfo.numOutputs > 1 ? ("out" + juce::String(i)) : juce::String("out"));
        text << "\n" << label << ": " << juce::String(editor.liveOutputValue(nodeId, i), 4);
    }
    return text;
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
