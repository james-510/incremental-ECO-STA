#include "paths/path_enumerator.hpp"
#include <algorithm>
#include <stdexcept>

namespace sta {
std::optional<TimingPath> worst_path(const TimingGraph& g, const TimingResult& r) {
  const auto n = g.nodes.size();
  if (r.arrival.size() != n || r.required.size() != n || r.slack.size() != n ||
      r.predecessor.size() != n)
    throw std::invalid_argument("result size does not match graph");
  std::optional<NodeId> end;
  for (NodeId v = 0; v < n; ++v) {
    if (!g.nodes[v].required_time || !r.slack[v]) continue;
    // Ascending NodeId scan breaks equal-slack endpoint ties reproducibly.
    if (!end || *r.slack[v] < *r.slack[*end]) end = v;
  }
  if (!end) return std::nullopt;
  TimingPath p{{}, {}, *r.arrival[*end], *r.required[*end], *r.slack[*end]};
  auto v = *end;
  p.nodes.push_back(v);
  while (r.predecessor[v]) {
    const auto id = *r.predecessor[v];
    if (p.arcs.size() >= n || id >= g.arcs.size() || g.arcs[id].to != v)
      throw std::invalid_argument("invalid predecessor chain");
    p.arcs.push_back(id);
    v = g.arcs[id].from;
    if (v >= n) throw std::invalid_argument("invalid predecessor vertex");
    p.nodes.push_back(v);
  }
  std::reverse(p.nodes.begin(), p.nodes.end());
  std::reverse(p.arcs.begin(), p.arcs.end());
  return p;
}
} // namespace sta
