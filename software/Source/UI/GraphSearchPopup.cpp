#include "GraphSearchPopup.h"
#include "../DSP/Graph/NodeTypeRegistry.h"
#include "GraphEditorComponent.h"
#include "Palette.h"
#include "StyleHelpers.h"

namespace {
// Search only ever needs to surface "look up a sensor input" or "look up a synth parameter" -
// Math nodes are wiring/shaping utilities, not things searched for by name, and Constant is a
// literal-value generator rather than a real source.
bool isSearchableType(const Graph::NodeTypeInfo& info) {
    return (info.category == Graph::NodeCategory::Source || info.category == Graph::NodeCategory::Sink)
           && info.id != "math.constant";
}
} // namespace

GraphSearchPopup::GraphSearchPopup(GraphEditorComponent& editorIn) : editor(editorIn) {
    searchBox.setColour(juce::TextEditor::backgroundColourId, Palette::panel);
    searchBox.setColour(juce::TextEditor::textColourId, Palette::textHi);
    searchBox.setColour(juce::TextEditor::outlineColourId, Palette::border);
    searchBox.setTextToShowWhenEmpty("Search Source/Sink nodes...", Palette::textLo);
    searchBox.onTextChange = [this] { refilter(); };
    searchBox.onReturnKey = [this] { chooseResult(resultsList.getSelectedRow()); };
    searchBox.onEscapeKey = [this] { closePopup(); };
    searchBox.onArrowKey = [this](int delta) { moveSelection(delta); };
    addAndMakeVisible(searchBox);

    resultsList.setColour(juce::ListBox::backgroundColourId, Palette::panel);
    resultsList.setColour(juce::ListBox::outlineColourId, Palette::border);
    resultsList.setRowHeight(32);
    addAndMakeVisible(resultsList);

    setVisible(false);
}

void GraphSearchPopup::setContext(Graph::NodeGraph* graphIn, bool isEditable) {
    graph = graphIn;
    editable = isEditable;
    if (isVisible())
        refilter();
}

void GraphSearchPopup::open() {
    setVisible(true);
    toFront(true);
    searchBox.setText({}, juce::dontSendNotification);
    refilter();
    searchBox.grabKeyboardFocus();
}

void GraphSearchPopup::closePopup() {
    setVisible(false);
    editor.grabKeyboardFocus();
}

void GraphSearchPopup::refilter() {
    results.clear();

    const juce::String query = searchBox.getText().trim();
    constexpr int kMaxResults = 24;

    if (query.isNotEmpty() && graph != nullptr) {
        for (const auto& n : graph->nodes()) {
            const auto* info = Graph::NodeTypeRegistry::instance().find(n.typeId);
            if (info == nullptr || !isSearchableType(*info) || !info->displayName.containsIgnoreCase(query))
                continue;
            results.push_back({ true, n.id, {}, info->displayName, info->subcategory });
        }

        if (editable)
            for (const auto& info : Graph::NodeTypeRegistry::instance().all()) {
                if (!isSearchableType(info) || !info.displayName.containsIgnoreCase(query))
                    continue;
                results.push_back({ false, Graph::kInvalidNodeId, info.id, info.displayName, info.subcategory });
            }

        if (results.size() > static_cast<size_t>(kMaxResults))
            results.resize(static_cast<size_t>(kMaxResults));
    }

    resultsList.updateContent();
    if (results.empty())
        resultsList.deselectAllRows();
    else
        resultsList.selectRow(0);
}

void GraphSearchPopup::moveSelection(int delta) {
    if (results.empty())
        return;
    const int count = static_cast<int>(results.size());
    const int current = resultsList.getSelectedRow();
    const int newRow = juce::jlimit(0, count - 1, (current < 0 ? 0 : current) + delta);
    resultsList.selectRow(newRow);
}

void GraphSearchPopup::chooseResult(int row) {
    if (row < 0 || row >= static_cast<int>(results.size()))
        return;
    const SearchResult& r = results[static_cast<size_t>(row)];
    if (r.isExistingNode)
        editor.goToNode(r.nodeId);
    else
        editor.addNodeAtViewCentre(r.typeId);
    closePopup();
}

void GraphSearchPopup::ResultsListModel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= static_cast<int>(owner.results.size()))
        return;
    const SearchResult& r = owner.results[static_cast<size_t>(row)];

    g.fillAll(selected ? Palette::accentDim : Palette::panel);

    g.setColour(r.isExistingNode ? Palette::accent : Palette::green);
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText(r.isExistingNode ? "GO TO" : "ADD", juce::Rectangle<int>(8, 0, 50, h),
               juce::Justification::centredLeft);

    g.setColour(Palette::textHi);
    g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
    g.drawText(r.label, juce::Rectangle<int>(60, 0, w - 130, h), juce::Justification::centredLeft);

    g.setColour(Palette::textLo);
    g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
    g.drawText(r.subLabel, juce::Rectangle<int>(w - 90, 0, 82, h), juce::Justification::centredRight);
}

void GraphSearchPopup::paint(juce::Graphics& g) {
    g.setColour(Palette::panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
    g.setColour(Palette::border);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void GraphSearchPopup::resized() {
    auto bounds = getLocalBounds().reduced(8);
    searchBox.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(6);
    resultsList.setBounds(bounds);
}
