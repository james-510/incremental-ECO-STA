#pragma once
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include "mock_backend.hpp"
#include "sta/incremental_timing.hpp"
#include "eco/eco_types.hpp"
#include "paths/path_enumerator.hpp"

namespace integration {
// Test scaffolding, not the final parser/Liberty interface. One scalar delay per
// combinational cell; every input connects to every output; net delays are zero.
// No transition sense, slew/load, clocks or sequential checks.
struct FixedModel {
    std::unordered_map<eco::CellID, sta::Time> cell_delay;
    std::unordered_map<eco::PinID, sta::Time> input_arrival;
    std::unordered_map<eco::PinID, sta::Time> required_time;
};

struct PinGraph {
    sta::TimingGraph graph;
    std::unordered_map<eco::PinID, sta::NodeId> pin_to_node;
    std::vector<eco::PinID> node_to_pin;
};

inline PinGraph build_pin_graph(const eco::MockBackend& design, const FixedModel& model) {
    const auto diagnostic = design.invariants();
    if (!diagnostic.empty()) throw std::invalid_argument(diagnostic);
    for (auto inst : design.aliveInsts()) {
        if (design.isSequentialCell(design.cellOf(inst)))
            throw std::invalid_argument("mock timing adapter supports combinational cells only");
    }
    PinGraph result;
    for (auto pin : design.allAlivePins()) {
        const auto node = result.graph.nodes.size();
        result.pin_to_node.emplace(pin, node);
        result.node_to_pin.push_back(pin);
        result.graph.nodes.push_back({design.pinName(pin), {}, {}});
    }
    auto node_of = [&](eco::PinID pin) {
        const auto it = result.pin_to_node.find(pin);
        if (it == result.pin_to_node.end())
            throw std::invalid_argument("timing configuration references a non-live pin");
        return it->second;
    };
    for (const auto& boundary : model.input_arrival)
        result.graph.nodes[node_of(boundary.first)].input_arrival = boundary.second;
    for (const auto& boundary : model.required_time)
        result.graph.nodes[node_of(boundary.first)].required_time = boundary.second;

    // Gather physical connections separately from pin-to-pin cell dependencies.
    for (auto pin : result.node_to_pin) {
        if (!design.isOutputPin(pin)) continue;
        const auto net = design.netOf(pin);
        if (net == eco::kInvalid) continue;
        for (auto sink : design.sinksOf(net))
            result.graph.arcs.push_back({node_of(pin), node_of(sink), 0.0, sta::ArcKind::Net});
    }
    for (auto inst : design.aliveInsts()) {
        const auto cell = design.cellOf(inst);
        if (design.cellDef(cell).port) continue;
        const auto delay = model.cell_delay.find(cell);
        if (delay == model.cell_delay.end())
            throw std::invalid_argument("missing fixed delay for cell " + design.cellDef(cell).name);
        if (!std::isfinite(delay->second))
            throw std::invalid_argument("fixed cell delay must be finite");
        for (auto input : design.inputPinsOf(inst))
            for (auto output : design.outputPinsOf(inst))
                result.graph.arcs.push_back({node_of(input), node_of(output), delay->second, sta::ArcKind::Cell});
    }
    return result;
}

struct Snapshot {
    PinGraph mapped;
    sta::TimingResult timing;
};

// Rebuild on EVERY call, including swaps and undo. Never label this incremental.
inline Snapshot recalculate(const eco::MockBackend& design, const FixedModel& model) {
    auto mapped = build_pin_graph(design, model);
    auto timing = sta::analyze_full(mapped.graph);
    return {std::move(mapped), std::move(timing)};
}

inline bool same_time(std::optional<sta::Time> a, std::optional<sta::Time> b) {
    return a.has_value() == b.has_value() && (!a || std::abs(*a - *b) < 1e-9);
}

// Compare by stable pin ID, not by graph-vector position. Snapshots own values.
inline bool same_timing(const Snapshot& a, const Snapshot& b) {
    if (a.mapped.node_to_pin.size() != b.mapped.node_to_pin.size()) return false;
    for (auto pin : a.mapped.node_to_pin) {
        const auto found = b.mapped.pin_to_node.find(pin);
        if (found == b.mapped.pin_to_node.end()) return false;
        const auto u = a.mapped.pin_to_node.at(pin), v = found->second;
        if (!same_time(a.timing.arrival[u], b.timing.arrival[v]) ||
            !same_time(a.timing.required[u], b.timing.required[v]) ||
            !same_time(a.timing.slack[u], b.timing.slack[v])) return false;
        const auto pa = a.timing.predecessor[u], pb = b.timing.predecessor[v];
        if (pa.has_value() != pb.has_value()) return false;
        if (pa) {
            const auto& ea = a.mapped.graph.arcs[*pa];
            const auto& eb = b.mapped.graph.arcs[*pb];
            if (a.mapped.node_to_pin[ea.from] != b.mapped.node_to_pin[eb.from] ||
                ea.kind != eb.kind || ea.delay != eb.delay) return false;
        }
    }
    return true;
}

// Prototype synchronization: build/scan a candidate graph to detect changes.
// Propagation is incremental for unchanged topology, but this O(V+E) adapter
// synchronization is NOT incremental. No end-to-end speedup claim is made.
class MockTimingSession {
public:
    MockTimingSession(const eco::MockBackend& design, const FixedModel& model)
        : design_(design), model_(model), mapped_(build_pin_graph(design, model)), engine_(mapped_.graph) {}

    Snapshot snapshot() const { return {mapped_, engine_.result()}; }

    sta::UpdateStats full_update() {
        auto next = build_pin_graph(design_, model_);
        auto stats = engine_.reset(next.graph);
        mapped_ = std::move(next);
        return stats;
    }

    sta::UpdateStats update(const eco::DirtySeeds& seeds) {
        auto next = build_pin_graph(design_, model_);
        if (seeds.structural || !same_topology(mapped_, next)) {
            auto stats = engine_.reset(next.graph);
            mapped_ = std::move(next);
            return stats;
        }
        sta::TimingUpdate changes;
        const auto& old = engine_.graph();
        for (sta::ArcId id = 0; id < old.arcs.size(); ++id)
            if (old.arcs[id].delay != next.graph.arcs[id].delay)
                changes.arc_delays.push_back({id, next.graph.arcs[id].delay});
        for (sta::NodeId v = 0; v < old.nodes.size(); ++v) {
            if (old.nodes[v].input_arrival != next.graph.nodes[v].input_arrival)
                changes.input_arrivals.push_back({v, next.graph.nodes[v].input_arrival});
            if (old.nodes[v].required_time != next.graph.nodes[v].required_time)
                changes.required_times.push_back({v, next.graph.nodes[v].required_time});
        }
        for (auto p : seeds.arrival) changes.arrival_seeds.push_back(next.pin_to_node.at(p));
        for (auto p : seeds.requirement) changes.required_seeds.push_back(next.pin_to_node.at(p));
        auto stats = engine_.update(changes);
        mapped_ = std::move(next);
        return stats;
    }

private:
    static bool same_topology(const PinGraph& a, const PinGraph& b) {
        if (a.node_to_pin != b.node_to_pin || a.graph.arcs.size() != b.graph.arcs.size()) return false;
        for (sta::NodeId v = 0; v < a.graph.nodes.size(); ++v)
            if (a.graph.nodes[v].name != b.graph.nodes[v].name) return false;
        for (sta::ArcId id = 0; id < a.graph.arcs.size(); ++id) {
            const auto& x = a.graph.arcs[id]; const auto& y = b.graph.arcs[id];
            if (x.from != y.from || x.to != y.to || x.kind != y.kind) return false;
        }
        return true;
    }
    const eco::MockBackend& design_;
    const FixedModel& model_;
    PinGraph mapped_;
    sta::IncrementalTiming engine_;
};
} // namespace integration
