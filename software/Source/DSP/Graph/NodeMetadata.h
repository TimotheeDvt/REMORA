#pragma once

#include "NodeTypeRegistry.h"

namespace Graph {

// To add a new node, add one entry to NodeMetadata.cpp's buildAllNodes() - nothing else needs to change.
void registerAllNodes(NodeTypeRegistry& registry);

} // namespace Graph
