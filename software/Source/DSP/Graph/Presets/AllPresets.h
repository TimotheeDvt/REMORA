#pragma once

#include "../NodeGraph.h"
#include <memory>

// One function per mapping strategy, each reproducing its behavior as a NodeGraph.
namespace Graph::Presets {

std::unique_ptr<NodeGraph> buildSimple();
std::unique_ptr<NodeGraph> buildLeadDrone();
std::unique_ptr<NodeGraph> buildSpinFilter();
std::unique_ptr<NodeGraph> buildBowedChord();
std::unique_ptr<NodeGraph> buildAzimut();
std::unique_ptr<NodeGraph> buildAzimutPlus();
std::unique_ptr<NodeGraph> buildAzimutReverb();
std::unique_ptr<NodeGraph> buildBozendo();
std::unique_ptr<NodeGraph> buildBozendo2();
std::unique_ptr<NodeGraph> buildSpinVoice();
std::unique_ptr<NodeGraph> buildBens();

} // namespace Graph::Presets
