#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace sta {
using NodeId = std::size_t;
using ArcId = std::size_t;
using Time = double; // All times are in ns in this starter.

// Engine-side contract, manually populated by samples.cpp today.
// Later your graph builder populates it from the parser's Design/Library data.
struct Node {
  std::string name;
  std::optional<Time> input_arrival; // Only permitted at graph sources.
  std::optional<Time> required_time; // Only permitted at graph sinks.
};
enum class ArcKind { Cell, Net };
struct Arc {
  NodeId from;
  NodeId to;
  Time delay;
  ArcKind kind = ArcKind::Cell; // Informational for this fixed-delay model.
};
struct TimingGraph {
  std::vector<Node> nodes; // IDs are vector indices, stable until deletion/reorder.
  std::vector<Arc> arcs;
};
} // namespace sta
