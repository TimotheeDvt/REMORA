#pragma once

#include "../DSP/Graph/NodeGraph.h"
#include <JuceHeader.h>
#include <vector>

class GraphEditorComponent;

// Floating search overlay, opened with Ctrl/Cmd+F over the graph editor. Lets the user jump to an
// existing Source/Sink node on the canvas, or (when the graph is editable) drop a new one - Math
// nodes and the Constant generator are deliberately left out, since they're not something you'd
// "look up" by name the way a sensor input or a synth parameter is.
class GraphSearchPopup : public juce::Component {
public:
    explicit GraphSearchPopup(GraphEditorComponent& editor);

    void setContext(Graph::NodeGraph* graph, bool isEditable);
    void open();
    void closePopup();
    bool isOpen() const noexcept { return isVisible(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct SearchResult {
        bool isExistingNode = false;
        Graph::NodeId nodeId = Graph::kInvalidNodeId; // valid when isExistingNode
        juce::String typeId;                          // valid when !isExistingNode
        juce::String label;
        juce::String subLabel;
    };

    class ResultsListModel : public juce::ListBoxModel {
    public:
        explicit ResultsListModel(GraphSearchPopup& ownerIn) : owner(ownerIn) {}
        int getNumRows() override { return static_cast<int>(owner.results.size()); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override { owner.chooseResult(row); }

    private:
        GraphSearchPopup& owner;
    };

    class SearchTextEditor : public juce::TextEditor {
    public:
        std::function<void(int)> onArrowKey; // argument: -1 (up) or +1 (down)
        bool keyPressed(const juce::KeyPress& key) override {
            if (onArrowKey && (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)) {
                onArrowKey(key == juce::KeyPress::downKey ? 1 : -1);
                return true;
            }
            return juce::TextEditor::keyPressed(key);
        }
    };

    GraphEditorComponent& editor;
    Graph::NodeGraph* graph = nullptr;
    bool editable = false;

    SearchTextEditor searchBox;
    ResultsListModel resultsModel { *this };
    juce::ListBox resultsList { "GraphSearchResults", &resultsModel };
    std::vector<SearchResult> results;

    void refilter();
    void moveSelection(int delta);
    void chooseResult(int row);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphSearchPopup)
};
