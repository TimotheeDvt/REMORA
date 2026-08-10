#include "GraphMappingStrategy.h"
#include "NodeTypeRegistry.h"

namespace Graph {

GraphMappingStrategy::GraphMappingStrategy(std::unique_ptr<NodeGraph> graph, juce::String displayName, juce::String description)
    : name_(std::move(displayName)), description_(std::move(description)), graph_(std::move(graph)) {
    const NodeTypeRegistry& registry = NodeTypeRegistry::instance();
    for (const auto& node : graph_->nodes()) {
        const NodeTypeInfo* info = registry.find(node.typeId);
        if (info == nullptr || info->category != NodeCategory::Sink)
            continue;
        MonitorParam& param = addMonitorParam(info->displayName, node.typeId, info->monitorRangeMin, info->monitorRangeMax);
        sinkMonitors_.emplace_back(node.id, &param);
    }
}

void GraphMappingStrategy::prepare(double sampleRate) {
    motion_.prepare();
    graph_->prepare(sampleRate);
}

void GraphMappingStrategy::process(const StaffSoundParams& in, MappingOutput& out) {
    const StaffMotionAnalyzer::DerivedMotionFrame frame = motion_.computeFrame(in);
    const SourceFrame sourceFrame{ in, frame, motion_ };
    graph_->evaluate(sourceFrame, out);

    for (auto& monitor : sinkMonitors_)
        monitor.second->value.store(graph_->outputOf(monitor.first), std::memory_order_relaxed);
}

} // namespace Graph
