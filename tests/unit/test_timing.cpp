#include "samples.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

static void check(bool ok, const char* msg) {
  if (!ok) throw std::runtime_error(msg);
}
static bool near(std::optional<double> a, double b) {
  return a && std::abs(*a - b) < 1e-9;
}
static void rejects(const sta::TimingGraph& g) {
  bool rejected = false;
  try { (void)sta::analyze_full(g); }
  catch (const std::invalid_argument&) { rejected = true; }
  check(rejected, "invalid graph accepted");
}

int main() {
  try {
    auto g = sta::sample("budget_compare");
    auto r = sta::analyze_full(g);
    const double a[] = {1, 0, 4, 2, 7, 4};
    const double req[] = {1.5, 1, 5, 2.5, 8, 4.5};
    for (std::size_t v = 0; v < 6; ++v) {
      check(near(r.arrival[v], a[v]), "budget arrival mismatch");
      check(near(r.required[v], req[v]), "budget required mismatch");
      check(near(r.slack[v], req[v]-a[v]), "budget slack mismatch");
    }
    auto p = sta::worst_path(g, r);
    check(p && p->nodes == std::vector<sta::NodeId>({0, 3, 5}) &&
          near(p->slack, 0.5), "wrong budget-ranked path");
    g = sta::sample("violations"); r = sta::analyze_full(g);
    p = sta::worst_path(g, r);
    check(p && p->nodes == std::vector<sta::NodeId>({1, 2, 4}) &&
          near(p->slack, -1), "negative slack ranking failed");
    g = sta::sample("missing_boundaries"); r = sta::analyze_full(g);
    check(near(r.arrival[1], 2) && !r.required[1] && !r.slack[1],
          "unconstrained node assigned a deadline");
    check(!r.arrival[3] && near(r.required[3], 5) && !r.slack[3],
          "unreachable node assigned an arrival");
    check(!sta::worst_path(g, r), "invented a constrained path");
    g = sta::sample("ties"); r = sta::analyze_full(g);
    check(r.predecessor[2] == 0, "tie is not reproducible");
    // Parallel arcs require storing the predecessor ARC, not only its vertex.
    g.arcs.push_back({0, 2, 1.5}); r = sta::analyze_full(g);
    check(r.predecessor[2] == 2 && near(r.arrival[2], 1.5), "parallel arcs failed");
    rejects(sta::sample("cycle"));
    g = sta::sample("ties"); g.arcs[0].to = 999; rejects(g);
    g = sta::sample("ties"); g.arcs[0].delay = std::numeric_limits<double>::quiet_NaN(); rejects(g);
    g = sta::sample("ties"); g.nodes[1].name = "a"; rejects(g);
    g = sta::sample("ties"); g.nodes[2].input_arrival = 0; rejects(g);
    g = sta::sample("ties"); g.nodes[0].required_time = 1; rejects(g);
    check(!sta::worst_path({}, sta::analyze_full({})), "empty graph failed");

    // Independent oracle for tiny graphs: explicitly enumerate all source-to-
    // sink paths, rather than duplicating the topological propagation algorithm.
    std::mt19937 rng(12345);
    for (int trial = 0; trial < 150; ++trial) {
      const std::size_t n = 2 + rng() % 7;
      g = {};
      std::vector<int> incoming(n, 0), outgoing(n, 0);
      for (std::size_t v = 0; v < n; ++v)
        g.nodes.push_back({"n" + std::to_string(v), {}, {}});
      for (std::size_t u = 0; u < n; ++u)
        for (std::size_t v = u+1; v < n; ++v)
          if (rng() % 3 == 0) {
            g.arcs.push_back({u, v, static_cast<double>(rng() % 5)});
            ++incoming[v]; ++outgoing[u];
          }
      for (std::size_t v = 0; v < n; ++v) {
        if (!incoming[v] && rng() % 3 != 0) g.nodes[v].input_arrival = rng() % 4;
        if (!outgoing[v] && rng() % 3 != 0) g.nodes[v].required_time = 4 + rng() % 9;
      }
      r = sta::analyze_full(g);
      std::vector<std::optional<double>> expected_a(n), expected_r(n);
      std::function<void(sta::NodeId, double)> walk_a = [&](auto u, double t) {
        if (!expected_a[u] || t > *expected_a[u]) expected_a[u] = t;
        for (const auto& e : g.arcs) if (e.from == u) walk_a(e.to, t+e.delay);
      };
      std::function<void(sta::NodeId, double)> walk_r = [&](auto v, double t) {
        if (!expected_r[v] || t < *expected_r[v]) expected_r[v] = t;
        for (const auto& e : g.arcs) if (e.to == v) walk_r(e.from, t-e.delay);
      };
      for (sta::NodeId v = 0; v < n; ++v) {
        if (g.nodes[v].input_arrival) walk_a(v, *g.nodes[v].input_arrival);
        if (g.nodes[v].required_time) walk_r(v, *g.nodes[v].required_time);
      }
      std::optional<double> minimum_slack;
      for (sta::NodeId v = 0; v < n; ++v) {
        check(r.arrival[v] == expected_a[v], "enumeration arrival disagreement");
        check(r.required[v] == expected_r[v], "enumeration required disagreement");
        if (expected_a[v] && expected_r[v])
          check(near(r.slack[v], *expected_r[v]-*expected_a[v]), "enumeration slack disagreement");
        else check(!r.slack[v], "unexpected valid slack");
        if (g.nodes[v].required_time && r.slack[v] &&
            (!minimum_slack || *r.slack[v] < *minimum_slack)) minimum_slack = r.slack[v];
      }
      p = sta::worst_path(g, r);
      check(p.has_value() == minimum_slack.has_value(), "path existence disagreement");
      if (p) {
        check(near(p->slack, *minimum_slack), "path ranking disagreement");
        double t = *g.nodes[p->nodes.front()].input_arrival;
        for (auto id : p->arcs) t += g.arcs[id].delay;
        check(near(t, p->arrival), "path reconstruction sum disagreement");
      }
    }
    std::cout << "PASS: named cases, invalid inputs, and 150 seeded DAGs checked by path enumeration\n";
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
