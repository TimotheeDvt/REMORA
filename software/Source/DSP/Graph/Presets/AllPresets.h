#pragma once

#include "../NodeGraph.h"
#include <memory>

// One build*Graph() function per retired hand-written mapping strategy (see
// Source/DSP/Mappings/*, now removed) - each returns a NodeGraph that
// reproduces that mapping's behavior, fed into a GraphMappingStrategy from
// BoStaffSynth.cpp. See /home/kadora/.claude/plans/vectorized-mapping-kahan.md.
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
