#pragma once
#include "sta/timing_engine.hpp"

namespace sta {
struct TimingPath {
  std::vector<NodeId> nodes;
  std::vector<ArcId> arcs;
  Time arrival;
  Time required;
  Time slack;
};


// Top-1 only. Pass the unchanged graph used to obtain result.
// Returns nullopt when no constrained endpoint has an initialized source path.
std::optional<TimingPath> worst_path(const TimingGraph& graph,
                                     const TimingResult& result);
} // namespace sta
