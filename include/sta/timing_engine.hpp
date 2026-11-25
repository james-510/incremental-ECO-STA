#pragma once
#include "graph/timing_graph.hpp"

namespace sta {
struct TimingResult {
  std::vector<NodeId> topological_order;
  std::vector<std::optional<Time>> arrival;
  std::vector<std::optional<Time>> required;
  std::vector<std::optional<Time>> slack;
  std::vector<std::optional<ArcId>> predecessor;
};

// Every call recomputes from scratch; graph is borrowed read-only.
// Throws invalid_argument for invalid data/cycles, overflow_error for overflow.
TimingResult analyze_full(const TimingGraph& graph);
} // namespace sta
