#include "sta/incremental_timing.hpp"
#include "paths/path_enumerator.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static void matches_full(const sta::IncrementalTiming& timer) {
    const auto full = sta::analyze_full(timer.graph());
    const auto& inc = timer.result();
    check(inc.arrival == full.arrival, "incremental/full arrival mismatch");
    check(inc.required == full.required, "incremental/full required mismatch");
    check(inc.slack == full.slack, "incremental/full slack mismatch");
    check(inc.predecessor == full.predecessor, "incremental/full predecessor mismatch");
    check(inc.topological_order == full.topological_order, "topological order changed");
    const auto a = sta::worst_path(timer.graph(), inc), b = sta::worst_path(timer.graph(), full);
    check(a.has_value() == b.has_value(), "incremental/full path existence mismatch");
    if (a) check(a->nodes == b->nodes && a->arcs == b->arcs && a->arrival == b->arrival &&
                 a->required == b->required && a->slack == b->slack, "incremental/full path mismatch");
}

static sta::TimingGraph reconvergent() {
    return {{{"a", 0.0, {}}, {"b", 0.0, {}}, {"x", {}, {}}, {"out", {}, 10.0},
             {"separate", 0.0, {}}, {"separate_out", {}, 12.0}},
            {{0, 2, 5.0}, {1, 2, 3.0}, {2, 3, 1.0}, {4, 5, 2.0}}};
}

static void named_cases() {
    sta::IncrementalTiming timer(reconvergent());
    sta::TimingUpdate update;
    update.arc_delays = {{0, 1.0}};
    auto stats = timer.update(update);
    matches_full(timer);
    check(timer.result().arrival[3] == 4.0 && timer.result().predecessor[2] == 1,
          "delay decrease failed to select alternative path");
    check(!stats.full_recompute && stats.visited_arrival == 2 && stats.visited_required == 1,
          "update unnecessarily visited unrelated nodes");
    update = {}; update.arc_delays = {{0, 3.0}};
    stats = timer.update(update); matches_full(timer);
    check(timer.result().predecessor[2] == 0 && stats.changed_predecessor == 1 &&
          stats.changed_arrival == 0 && stats.visited_arrival == 1,
          "equal arrival did not update predecessor or prune numeric propagation");

    // Non-winning arc changes still affect required time at its input.
    timer.reset(reconvergent());
    update = {}; update.arc_delays = {{1, 4.0}};
    stats = timer.update(update); matches_full(timer);
    check(stats.changed_arrival == 0 && stats.visited_arrival == 1 &&
          timer.result().required[1] == 5.0, "arrival pruning lost a required-time dependency");

    update = {}; update.input_arrivals = {{0, {}}};
    timer.update(update); matches_full(timer);
    check(!timer.result().arrival[0] && timer.result().predecessor[2] == 1, "clearing source failed");
    update = {}; update.input_arrivals = {{1, {}}};
    timer.update(update); matches_full(timer);
    check(!timer.result().arrival[3] && !timer.result().slack[3] && !timer.result().predecessor[2],
          "unreachable branch retained timing state");
    update = {}; update.input_arrivals = {{0, -0.5}}; update.required_times = {{3, {}}};
    timer.update(update); matches_full(timer);
    check(!timer.result().required[0] && !timer.result().slack[3], "clearing endpoint failed");
    update = {}; update.required_times = {{3, 1.0}};
    timer.update(update); matches_full(timer);
    check(sta::worst_path(timer.graph(), timer.result())->slack < 0, "negative-slack ranking failed");

    stats = timer.update({});
    check(stats.visited_arrival == 0 && stats.visited_required == 0 && stats.recomputed_slack == 0,
          "no-op update performed propagation");
    update = {}; update.arrival_seeds = {0, 0}; update.required_seeds = {3, 3};
    stats = timer.update(update); matches_full(timer);
    check(stats.visited_arrival == 1 && stats.visited_required == 1,
          "unchanged/duplicate seeds propagated unnecessarily");

    // Parallel arcs and identical arrival ties retain the smaller ArcId.
    auto g = reconvergent(); g.arcs.push_back({0, 2, 5.0});
    timer.reset(g);
    update = {}; update.arc_delays = {{0, 4.0}};
    timer.update(update); matches_full(timer);
    check(timer.result().predecessor[2] == 4, "parallel arc not selected");
    update = {}; update.arc_delays = {{0, 5.0}};
    timer.update(update); matches_full(timer);
    check(timer.result().predecessor[2] == 0, "parallel-arc tie not deterministic");

    // Full recomputation is explicit for a changed topology, including empty.
    stats = timer.reset({});
    check(stats.full_recompute && !sta::worst_path(timer.graph(), timer.result()), "empty reset failed");
    matches_full(timer);
}

static void invalid_cases() {
    sta::IncrementalTiming timer(reconvergent());
    const auto original = timer.result().arrival;
    auto rejects = [&](const sta::TimingUpdate& changes) {
        bool rejected = false;
        try { timer.update(changes); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && timer.valid() && timer.result().arrival == original,
              "invalid edit changed/invalidated valid timing");
        matches_full(timer);
    };
    sta::TimingUpdate u;
    u.arc_delays = {{0, 2.0}, {999, 1.0}}; rejects(u); // Validate the entire batch first.
    u = {}; u.arc_delays = {{0, std::numeric_limits<double>::infinity()}}; rejects(u);
    u = {}; u.input_arrivals = {{2, 0.0}}; rejects(u);
    u = {}; u.required_times = {{0, 1.0}}; rejects(u);
    u = {}; u.arrival_seeds = {999}; rejects(u);
    u = {}; u.required_seeds = {999}; rejects(u);
    u = {}; u.input_arrivals = {{0, std::numeric_limits<double>::quiet_NaN()}}; rejects(u);
    bool rejected = false;
    try { auto g = reconvergent(); g.arcs.push_back({3, 0, 0.0}); timer.reset(g); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && timer.valid(), "invalid full reset damaged existing cache");
    matches_full(timer);

    const auto huge = std::numeric_limits<double>::max();
    timer.reset({{{"a", 0.0, {}}, {"out", {}, {}}}, {{0, 1, huge}}});
    u = {}; u.input_arrivals = {{0, huge}};
    rejected = false;
    try { timer.update(u); } catch (const std::overflow_error&) { rejected = true; }
    check(rejected && !timer.valid(), "overflow left a readable stale cache");
    rejected = false;
    try { (void)timer.result(); } catch (const std::logic_error&) { rejected = true; }
    check(rejected, "invalid result was exposed");
    rejected = false;
    try { timer.update({}); } catch (const std::logic_error&) { rejected = true; }
    check(rejected, "invalid cache accepted further update");
    timer.reset(reconvergent()); matches_full(timer);
}

static void randomized_cases() {
    std::mt19937 rng(92861);
    for (int trial = 0; trial < 100; ++trial) {
        const std::size_t n = 2 + rng() % 10;
        sta::TimingGraph g;
        std::vector<std::size_t> indegree(n, 0), outdegree(n, 0);
        for (std::size_t v = 0; v < n; ++v) g.nodes.push_back({"n" + std::to_string(v), {}, {}});
        for (sta::NodeId a = 0; a < n; ++a)
            for (sta::NodeId b = a + 1; b < n; ++b)
                if (rng() % 3 == 0) {
                    g.arcs.push_back({a, b, static_cast<double>(rng() % 9) / 4.0 - 0.5});
                    ++indegree[b]; ++outdegree[a];
                }
        std::vector<sta::NodeId> sources, sinks;
        for (sta::NodeId v = 0; v < n; ++v) {
            if (!indegree[v]) { sources.push_back(v); g.nodes[v].input_arrival = 0.0; }
            if (!outdegree[v]) { sinks.push_back(v); g.nodes[v].required_time = 5.0; }
        }
        sta::IncrementalTiming timer(g);
        for (int step = 0; step < 30; ++step) {
            sta::TimingUpdate u;
            // Batch multiple edits and repeatedly clear/restore boundaries.
            if (!g.arcs.empty()) {
                u.arc_delays.push_back({rng() % g.arcs.size(), static_cast<double>(rng() % 17) / 4.0 - 1.0});
                u.arc_delays.push_back({rng() % g.arcs.size(), static_cast<double>(rng() % 17) / 4.0 - 1.0});
            }
            const auto a = sources[rng() % sources.size()], b = sinks[rng() % sinks.size()];
            u.input_arrivals.push_back({a, rng() % 4 == 0 ? std::nullopt : std::optional<double>(rng() % 8 / 4.0)});
            u.required_times.push_back({b, rng() % 4 == 0 ? std::nullopt : std::optional<double>(rng() % 24 / 4.0)});
            if (step % 5 == 0) { u.arrival_seeds = {a, a}; u.required_seeds = {b, b}; }
            timer.update(u); matches_full(timer);
        }
    }
}

int main() {
    try {
        named_cases(); invalid_cases(); randomized_cases();
        std::cout << "PASS: incremental pruning/ties/boundaries/errors and 3000 seeded edit batches vs full timing\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
