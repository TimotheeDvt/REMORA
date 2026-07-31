#pragma once

#include "NodeTypeRegistry.h"

namespace Graph {

// NodeMetadata.cpp is the single list of every node type: id, display name,
// category, subcategory, description, input/param/output names, default
// params, monitor range, AND how it actually computes its output (its
// eval/write function). Every node id exists in exactly one place: that
// file's buildAllNodes() list.
//
// To add a new node: add one entry to buildAllNodes() with its data and
// logic together, using the math()/source()/sink() builder for its shape.
// Nothing else needs to change - registerAllNodes() below creates it
// automatically.
void registerAllNodes(NodeTypeRegistry& registry);

} // namespace Graph
