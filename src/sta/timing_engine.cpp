#include "sta/timing_engine.hpp"
#include <cmath>
#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace sta {
TimingResult analyze_full(const TimingGraph& g) {
  const auto n = g.nodes.size();
  std::vector<std::vector<ArcId>> out(n);
  std::vector<std::size_t> indegree(n, 0);
  std::unordered_set<std::string> names;
  for (const auto& v : g.nodes) {
    if (v.name.empty() || !names.insert(v.name).second)
      throw std::invalid_argument("node names must be nonempty and unique");
    if ((v.input_arrival && !std::isfinite(*v.input_arrival)) ||
        (v.required_time && !std::isfinite(*v.required_time)))
      throw std::invalid_argument("boundary times must be finite");
  }
  for (ArcId id = 0; id < g.arcs.size(); ++id) {
    const auto& e = g.arcs[id];
    if (e.from >= n || e.to >= n)
      throw std::invalid_argument("arc endpoint ID out of range");
    if (!std::isfinite(e.delay))
      throw std::invalid_argument("arc delay must be finite");
    out[e.from].push_back(id);
    ++indegree[e.to];
  }
  std::queue<NodeId> ready;
  for (NodeId v = 0; v < n; ++v) {
    if (g.nodes[v].input_arrival && indegree[v] != 0)
      throw std::invalid_argument("input arrival on a non-source: " + g.nodes[v].name);
    if (g.nodes[v].required_time && !out[v].empty())
      throw std::invalid_argument("endpoint deadline on a non-sink: " + g.nodes[v].name);
    if (indegree[v] == 0) ready.push(v);
  }
  TimingResult r;
  r.arrival.resize(n);
  r.required.resize(n);
  r.slack.resize(n);
  r.predecessor.resize(n);
  while (!ready.empty()) {
    const auto u = ready.front();
    ready.pop();
    r.topological_order.push_back(u);
    for (const auto id : out[u])
      if (--indegree[g.arcs[id].to] == 0) ready.push(g.arcs[id].to);
  }
  if (r.topological_order.size() != n)
    throw std::invalid_argument("combinational cycle detected");

  for (NodeId v = 0; v < n; ++v) {
    r.arrival[v] = g.nodes[v].input_arrival;
    r.required[v] = g.nodes[v].required_time;
  }
  // Sequential push implementation. Do not parallelize this outer loop:
  // different u values may write the same r.arrival[e.to].
  for (const auto u : r.topological_order) {
    if (!r.arrival[u]) continue;
    for (const auto id : out[u]) {
      const auto& e = g.arcs[id];
      const Time candidate = *r.arrival[u] + e.delay;
      if (!std::isfinite(candidate)) throw std::overflow_error("arrival overflow");
      if (!r.arrival[e.to] || candidate > *r.arrival[e.to] ||
          (candidate == *r.arrival[e.to] &&
           (!r.predecessor[e.to] || id < *r.predecessor[e.to]))) {
        r.arrival[e.to] = candidate;
        r.predecessor[e.to] = id;
      }
    }
  }
  for (auto it = r.topological_order.rbegin(); it != r.topological_order.rend(); ++it) {
    const auto u = *it;
    for (const auto id : out[u]) {
      const auto& e = g.arcs[id];
      if (!r.required[e.to]) continue;
      const Time candidate = *r.required[e.to] - e.delay;
      if (!std::isfinite(candidate)) throw std::overflow_error("required-time overflow");
      if (!r.required[u] || candidate < *r.required[u]) r.required[u] = candidate;
    }
    if (r.arrival[u] && r.required[u]) {
      const Time s = *r.required[u] - *r.arrival[u];
      if (!std::isfinite(s)) throw std::overflow_error("slack overflow");
      r.slack[u] = s;
    }
  }
  return r;
}

} // namespace sta
