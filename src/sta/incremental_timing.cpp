#include "sta/incremental_timing.hpp"
#include <cmath>
#include <functional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace sta {
IncrementalTiming::IncrementalTiming(TimingGraph graph) { reset(std::move(graph)); }

const TimingResult& IncrementalTiming::result() const {
    if (!valid_) throw std::logic_error("incremental cache invalid; call reset before reading timing");
    return result_;
}

UpdateStats IncrementalTiming::reset(TimingGraph graph) {
    // Build replacement state before invalidating an existing successful state.
    auto timing = analyze_full(graph);
    const auto n = graph.nodes.size();
    std::vector<std::vector<ArcId>> in(n), out(n);
    for (ArcId id = 0; id < graph.arcs.size(); ++id) {
        in[graph.arcs[id].to].push_back(id);
        out[graph.arcs[id].from].push_back(id);
    }
    std::vector<std::size_t> rank(n);
    for (std::size_t i = 0; i < n; ++i) rank[timing.topological_order[i]] = i;
    std::vector<unsigned char> qa(n, 0), qr(n, 0), ds(n, 0);
    graph_ = std::move(graph);
    result_ = std::move(timing);
    incoming_ = std::move(in); outgoing_ = std::move(out); rank_ = std::move(rank);
    queued_arrival_ = std::move(qa); queued_required_ = std::move(qr); dirty_slack_ = std::move(ds);
    valid_ = true;
    UpdateStats stats;
    stats.full_recompute = true;
    stats.visited_arrival = stats.visited_required = stats.recomputed_slack = n;
    return stats;
}

UpdateStats IncrementalTiming::update(const TimingUpdate& changes) {
    (void)result(); // A failed update must be recovered before another attempt.
    const auto n = graph_.nodes.size();
    auto check_node = [n](NodeId v) {
        if (v >= n) throw std::invalid_argument("incremental node ID out of range");
    };
    auto check_time = [](std::optional<Time> t) {
        if (t && !std::isfinite(*t)) throw std::invalid_argument("updated time must be finite");
    };
    for (const auto& edit : changes.arc_delays) {
        if (edit.arc >= graph_.arcs.size()) throw std::invalid_argument("updated arc ID out of range");
        check_time(edit.delay);
    }
    for (const auto& edit : changes.input_arrivals) {
        check_node(edit.node); check_time(edit.value);
        if (!incoming_[edit.node].empty()) throw std::invalid_argument("input arrival update on a non-source");
    }
    for (const auto& edit : changes.required_times) {
        check_node(edit.node); check_time(edit.value);
        if (!outgoing_[edit.node].empty()) throw std::invalid_argument("required-time update on a non-sink");
    }
    for (auto v : changes.arrival_seeds) check_node(v);
    for (auto v : changes.required_seeds) check_node(v);

    using Item = std::pair<std::size_t, NodeId>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> forward;
    std::priority_queue<Item> backward;
    std::vector<NodeId> slack_nodes;
    UpdateStats stats;
    // Any subsequent exception leaves a clearly invalid cache until reset().
    valid_ = false;
    auto push_arrival = [&](NodeId v) {
        if (!queued_arrival_[v]) { forward.push({rank_[v], v}); queued_arrival_[v] = 1; }
    };
    auto push_required = [&](NodeId v) {
        if (!queued_required_[v]) { backward.push({rank_[v], v}); queued_required_[v] = 1; }
    };
    auto mark_slack = [&](NodeId v) {
        if (!dirty_slack_[v]) { slack_nodes.push_back(v); dirty_slack_[v] = 1; }
    };
    for (const auto& edit : changes.arc_delays) {
        auto& arc = graph_.arcs[edit.arc];
        if (arc.delay == edit.delay) continue;
        arc.delay = edit.delay;
        push_arrival(arc.to);
        // Delay participates in R[from] = min(R[to] - delay), independently
        // of whether the maximum arrival at 'to' changes.
        push_required(arc.from);
    }
    for (const auto& edit : changes.input_arrivals) {
        auto& value = graph_.nodes[edit.node].input_arrival;
        if (value != edit.value) { value = edit.value; push_arrival(edit.node); }
    }
    for (const auto& edit : changes.required_times) {
        auto& value = graph_.nodes[edit.node].required_time;
        if (value != edit.value) { value = edit.value; push_required(edit.node); }
    }
    for (auto v : changes.arrival_seeds) push_arrival(v);
    for (auto v : changes.required_seeds) push_required(v);

    while (!forward.empty()) {
        const auto v = forward.top().second; forward.pop(); queued_arrival_[v] = 0;
        ++stats.visited_arrival;
        auto arrival = graph_.nodes[v].input_arrival;
        std::optional<ArcId> predecessor;
        for (auto id : incoming_[v]) {
            const auto& arc = graph_.arcs[id];
            if (!result_.arrival[arc.from]) continue;
            const Time candidate = *result_.arrival[arc.from] + arc.delay;
            if (!std::isfinite(candidate)) throw std::overflow_error("incremental arrival overflow");
            if (!arrival || candidate > *arrival ||
                (candidate == *arrival && (!predecessor || id < *predecessor))) {
                arrival = candidate;
                predecessor = id;
            }
        }
        if (predecessor != result_.predecessor[v]) ++stats.changed_predecessor;
        result_.predecessor[v] = predecessor;
        if (arrival == result_.arrival[v]) continue;
        result_.arrival[v] = arrival;
        ++stats.changed_arrival;
        mark_slack(v);
        for (auto id : outgoing_[v]) push_arrival(graph_.arcs[id].to);
    }
    while (!backward.empty()) {
        const auto v = backward.top().second; backward.pop(); queued_required_[v] = 0;
        ++stats.visited_required;
        auto required = graph_.nodes[v].required_time;
        for (auto id : outgoing_[v]) {
            const auto& arc = graph_.arcs[id];
            if (!result_.required[arc.to]) continue;
            const Time candidate = *result_.required[arc.to] - arc.delay;
            if (!std::isfinite(candidate)) throw std::overflow_error("incremental required-time overflow");
            if (!required || candidate < *required) required = candidate;
        }
        if (required == result_.required[v]) continue;
        result_.required[v] = required;
        ++stats.changed_required;
        mark_slack(v);
        for (auto id : incoming_[v]) push_required(graph_.arcs[id].from);
    }
    for (auto v : slack_nodes) {
        result_.slack[v].reset();
        if (result_.arrival[v] && result_.required[v]) {
            const Time value = *result_.required[v] - *result_.arrival[v];
            if (!std::isfinite(value)) throw std::overflow_error("incremental slack overflow");
            result_.slack[v] = value;
        }
        dirty_slack_[v] = 0;
        ++stats.recomputed_slack;
    }
    valid_ = true;
    return stats;
}
} // namespace sta
