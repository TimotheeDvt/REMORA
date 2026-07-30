#include "NodeTypeRegistry.h"

namespace Graph {

void registerSourceNodes(NodeTypeRegistry&);
void registerMathNodes(NodeTypeRegistry&);
void registerSinkNodes(NodeTypeRegistry&);

NodeTypeRegistry& NodeTypeRegistry::instance() {
    static NodeTypeRegistry registry;
    return registry;
}

NodeTypeRegistry::NodeTypeRegistry() {
    registerSourceNodes(*this);
    registerMathNodes(*this);
    registerSinkNodes(*this);
}

void NodeTypeRegistry::registerType(NodeTypeInfo info) {
    jassert(find(info.id) == nullptr && "duplicate node type id registered");
    indexById_[info.id.toStdString()] = types_.size();
    types_.push_back(std::move(info));
}

const NodeTypeInfo* NodeTypeRegistry::find(const juce::String& typeId) const {
    auto it = indexById_.find(typeId.toStdString());
    if (it == indexById_.end())
        return nullptr;
    return &types_[it->second];
}

} // namespace Graph
