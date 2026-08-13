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
        for (int p = 0; p < info->numInputs; ++p) {
            const juce::String label = info->numInputs > 1
                ? info->displayName + ": " + info->inputNames[static_cast<size_t>(p)]
                : info->displayName;
            const float lo = info->monitorRangeMin[static_cast<size_t>(p)];
            const float hi = info->monitorRangeMax[static_cast<size_t>(p)];
            MonitorParam& param = addMonitorParam(label, node.typeId, lo, hi);
            sinkMonitors_.push_back({ node.id, p, &param });
        }
    }
}

void GraphMappingStrategy::prepare(double sampleRate) {
    motion_.prepare();
    graph_->prepare(sampleRate);
}

bool GraphMappingStrategy::process(const StaffSoundParams& in, MappingOutput& out) {
    const StaffMotionAnalyzer::DerivedMotionFrame frame = motion_.computeFrame(in);
    const SourceFrame sourceFrame{ in, frame, motion_ };

    MappingOutput fresh;
    if (!graph_->evaluate(sourceFrame, fresh))
        return false;
    out = fresh;

    for (auto& monitor : sinkMonitors_) {
        float value;
        if (graph_->tryOutputOf(monitor.nodeId, monitor.port, value))
            monitor.param->value.store(value, std::memory_order_relaxed);
    }
    return true;
}

} // namespace Graph
