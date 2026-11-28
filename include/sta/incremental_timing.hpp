#pragma once
#include "sta/timing_engine.hpp"

namespace sta {
struct ArcDelayUpdate { ArcId arc; Time delay; };
struct BoundaryUpdate { NodeId node; std::optional<Time> value; };

struct TimingUpdate {
    std::vector<ArcDelayUpdate> arc_delays;
    std::vector<BoundaryUpdate> input_arrivals;
    std::vector<BoundaryUpdate> required_times;
    // Extra starting points from an external invalidator. Explicit changes above
    // always schedule their own dependencies, even if these lists are empty.
    std::vector<NodeId> arrival_seeds;
    std::vector<NodeId> required_seeds;
};

struct UpdateStats {
    bool full_recompute = false;
    std::size_t visited_arrival = 0;
    std::size_t visited_required = 0;
    std::size_t changed_arrival = 0;
    std::size_t changed_required = 0;
    std::size_t changed_predecessor = 0;
    std::size_t recomputed_slack = 0;
};

// Owns a fixed-topology graph and cached results. Scalar fixed delays only.
// update() changes delays/boundaries, never connectivity; reset() handles a new
// topology with full validation and recomputation. No ECO/parser dependency.
class IncrementalTiming {
public:
    explicit IncrementalTiming(TimingGraph graph);
    UpdateStats reset(TimingGraph graph);
    UpdateStats update(const TimingUpdate& changes);
    const TimingGraph& graph() const { return graph_; }
    const TimingResult& result() const;
    bool valid() const { return valid_; }

private:
    TimingGraph graph_;
    TimingResult result_;
    std::vector<std::vector<ArcId>> incoming_, outgoing_;
    std::vector<std::size_t> rank_;
    std::vector<unsigned char> queued_arrival_, queued_required_, dirty_slack_;
    bool valid_ = false;
};
// Validation errors before mutation preserve the cached result. Arithmetic or
// allocation failure during an update invalidates the cache; result()/update()
// then throw logic_error until a successful reset(). No stale result is exposed.
} // namespace sta
