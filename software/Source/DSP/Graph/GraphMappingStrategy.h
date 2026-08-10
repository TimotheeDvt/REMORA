#pragma once

#include "../IMappingStrategy.h"
#include "../StaffMotionAnalyzer.h"
#include "NodeGraph.h"
#include <utility>
#include <vector>

namespace Graph {

// The one IMappingStrategy left once every preset is a NodeGraph - auto-registers a MonitorParam per sink node.
class GraphMappingStrategy : public IMappingStrategy {
public:
    GraphMappingStrategy(std::unique_ptr<NodeGraph> graph, juce::String displayName, juce::String description = {});

    const char* getName() const override { return name_.toRawUTF8(); }
    const juce::String& getDescription() const override { return description_; }
    void prepare(double sampleRate) override;
    void process(const StaffSoundParams& in, MappingOutput& out) override;

    NodeGraph& getGraph() noexcept { return *graph_; }

private:
    juce::String name_;
    juce::String description_;
    StaffMotionAnalyzer motion_;
    std::unique_ptr<NodeGraph> graph_;
    std::vector<std::pair<NodeId, MonitorParam*>> sinkMonitors_;
};

} // namespace Graph
