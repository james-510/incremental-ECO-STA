#include <iostream>
#include <limits>
#include "eco/eco_manager.hpp"
#include "shared_fixture.hpp"

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static bool near(std::optional<double> value, double expected) {
    return integration::same_time(value, expected);
}
static sta::NodeId node(const integration::Snapshot& s, const eco::MockBackend& m, const char* name) {
    return s.mapped.pin_to_node.at(m.findPin(name));
}
static void rejects(const eco::MockBackend& m, const integration::FixedModel& model) {
    bool rejected = false;
    try { (void)integration::recalculate(m, model); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "invalid integration input accepted");
}

static void incremental_cases() {
    auto f = integration::make_fixture();
    auto& m = f.design;
    eco::ECOManager edits(m);
    integration::MockTimingSession timer(m, f.model);
    const auto before = timer.snapshot();
    const auto original_design = m.snapshot();
    auto matches_full = [&] {
        check(integration::same_timing(timer.snapshot(), integration::recalculate(m, f.model)),
              "mock incremental/full timing or predecessor mismatch");
    };
    check(edits.swapCell("u2", "NAND2_X2").ok(), "incremental swap failed");
    auto stats = timer.update(edits.takeSeeds());
    matches_full();
    check(!stats.full_recompute && stats.visited_arrival < before.mapped.graph.nodes.size() &&
          stats.visited_required < before.mapped.graph.nodes.size(), "swap did not limit propagation");
    auto after = timer.snapshot();
    check(near(after.timing.slack[node(after, m, "out1/P")], 0.45), "incremental swap result wrong");
    edits.undoAll();
    stats = timer.update(edits.takeSeeds()); matches_full();
    check(!stats.full_recompute && integration::same_timing(before, timer.snapshot()), "incremental undo failed");

    // Repeated increases/decreases and batched edits on the same topology.
    for (int i = 0; i < 40; ++i) {
        check(edits.swapCell("u2", i % 2 ? "NAND2_X1" : "NAND2_X2").ok(), "repeated swap failed");
        stats = timer.update(edits.takeSeeds()); matches_full();
        check(!stats.full_recompute, "nonstructural edit fell back unexpectedly");
    }
    check(edits.swapCell("u2", "NAND2_X2").ok(), "batched swap failed");
    check(edits.swapCell("u2", "NAND2_X1").ok(), "batched reverse swap failed");
    timer.update(edits.takeSeeds()); matches_full();
    stats = timer.update({});
    check(!stats.full_recompute && stats.visited_arrival == 0 && stats.visited_required == 0,
          "no-op mock update propagated");

    // This fixture uses explicit deadlines; clock-to-deadline translation is
    // supplied here rather than pretending the mock adapter supports SDC.
    check(edits.setClockPeriod(1.5).ok(), "period edit failed");
    f.model.required_time[m.findPin("out1/P")] = 1.5;
    f.model.required_time[m.findPin("out2/P")] = 1.3;
    stats = timer.update(edits.takeSeeds()); matches_full();
    check(!stats.full_recompute && stats.visited_arrival == 0 && stats.visited_required > 0,
          "deadline-only update performed forward work");
    f.model.input_arrival.erase(m.findPin("in1/P"));
    timer.update({}); matches_full(); // Explicit model diff schedules even without ECO seeds.
    f.model.input_arrival[m.findPin("in1/P")] = 0.1;
    timer.update({}); matches_full();
    edits.undoAll();
    f.model.required_time[m.findPin("out1/P")] = 1.0;
    f.model.required_time[m.findPin("out2/P")] = 0.8;
    timer.update(edits.takeSeeds()); matches_full();
    check(m.snapshot() == original_design && integration::same_timing(before, timer.snapshot()),
          "batched edits/period undo failed");

    auto buffer = edits.insertBuffer("n1", "BUF_X1", {"u2/A"});
    check(buffer.ok(), "incremental fixture insertion failed");
    stats = timer.update(edits.takeSeeds()); matches_full();
    check(stats.full_recompute, "structural edit not reported as full fallback");
    // Cached topology after fallback supports subsequent incremental changes.
    check(edits.swapCell("u2", "NAND2_X2").ok(), "swap after fallback failed");
    stats = timer.update(edits.takeSeeds()); matches_full();
    check(!stats.full_recompute, "post-fallback swap failed to use incremental engine");
    edits.undoAll();
    stats = timer.update(edits.takeSeeds()); matches_full();
    check(stats.full_recompute && m.snapshot() == original_design &&
          integration::same_timing(before, timer.snapshot()), "structural undo fallback failed");
    check(edits.reconnect("u2/A", "n_in2").ok(), "incremental reconnect failed");
    (void)edits.takeSeeds();
    stats = timer.update({}); matches_full();
    check(stats.full_recompute, "topology change without flag was not detected");
    edits.undoAll(); timer.update(edits.takeSeeds()); matches_full();
    eco::DirtySeeds structural_only; structural_only.structural = true;
    stats = timer.update(structural_only);
    check(stats.full_recompute, "structural flag was ignored because seed lists were empty");
}

int main() {
    try {
        incremental_cases();
        auto f = integration::make_fixture();
        auto& m = f.design;
        eco::ECOManager edits(m);
        const auto original_design = m.snapshot();
        const auto before = integration::recalculate(m, f.model);
        check(before.mapped.graph.nodes.size() == 11 && before.mapped.graph.arcs.size() == 10,
              "wrong pin-level graph size");
        check(before.mapped.pin_to_node.at(m.findPin("in1/P")) != m.findPin("in1/P"),
              "fixture failed to exercise non-dense pin IDs");
        const auto out1 = node(before, m, "out1/P");
        check(near(before.timing.arrival[out1], 0.7) && near(before.timing.required[out1], 1.0)
              && near(before.timing.slack[out1], 0.3), "hand-calculated baseline mismatch");
        auto path = sta::worst_path(before.mapped.graph, before.timing);
        check(path.has_value(), "baseline has no constrained path");
        std::vector<std::string> names;
        for (auto v : path->nodes) names.push_back(before.mapped.graph.nodes[v].name);
        check(names == std::vector<std::string>({"in1/P", "u1/A", "u1/ZN", "u2/A", "u2/ZN", "out1/P"}),
              "pin-level path reconstruction mismatch");

        check(edits.swapCell("u2", "NAND2_X2").ok(), "swap failed");
        auto seeds = edits.takeSeeds();
        check(!seeds.arrival.empty() && !seeds.requirement.empty() && !seeds.structural,
              "swap seed handoff failed");
        auto after = integration::recalculate(m, f.model);
        check(near(after.timing.arrival[node(after, m, "out1/P")], 0.55), "swap delay not refreshed");
        path = sta::worst_path(after.mapped.graph, after.timing);
        check(path && near(path->slack, 0.45), "hand-calculated after-swap slack mismatch");
        check(near(before.timing.arrival[out1], 0.7), "saved snapshot changed after edit");
        edits.undoAll();
        check(!edits.takeSeeds().empty(), "undo did not supply dirty seeds");
        check(m.snapshot() == original_design && integration::same_timing(before, integration::recalculate(m, f.model)),
              "swap undo failed to restore design/timing");

        // Structural edit: rebuild includes added pins; undo excludes tombstones.
        auto inserted = edits.insertBuffer("n1", "BUF_X1", {"u2/A"});
        check(inserted.ok(), "buffer insertion failed");
        check(edits.takeSeeds().structural, "buffer edit missing structural flag");
        after = integration::recalculate(m, f.model);
        check(after.mapped.graph.nodes.size() == 13 &&
              near(after.timing.arrival[node(after, m, "out1/P")], 0.75), "buffer topology/delay not refreshed");
        edits.undoAll(); (void)edits.takeSeeds();
        check(m.snapshot() == original_design && integration::same_timing(before, integration::recalculate(m, f.model)),
              "structural undo left stale pins/timing");

        // Reconnect reduces one branch enough for the other endpoint to be worst.
        check(edits.reconnect("u2/A", "n_in2").ok(), "reconnect failed");
        check(edits.takeSeeds().structural, "reconnect missing structural flag");
        after = integration::recalculate(m, f.model);
        path = sta::worst_path(after.mapped.graph, after.timing);
        check(near(after.timing.arrival[node(after, m, "out1/P")], 0.4) && path &&
              after.mapped.graph.nodes[path->nodes.back()].name == "out2/P" && near(path->slack, 0.5),
              "critical endpoint did not switch after reconnect");
        edits.undoAll(); (void)edits.takeSeeds();
        check(integration::same_timing(before, integration::recalculate(m, f.model)), "reconnect undo mismatch");

        auto model = f.model;
        model.required_time.erase(m.findPin("out2/P"));
        auto partial = integration::recalculate(m, model);
        check(!partial.timing.slack[node(partial, m, "out2/P")], "unconstrained output got a deadline");
        model = f.model; model.input_arrival.erase(m.findPin("in1/P"));
        partial = integration::recalculate(m, model);
        check(!partial.timing.arrival[node(partial, m, "out2/P")] &&
              near(partial.timing.arrival[node(partial, m, "out1/P")], 0.4), "uninitialized source got zero arrival");
        model = f.model; model.cell_delay.erase(m.findCell("NAND2_X1")); rejects(m, model);
        model = f.model; model.cell_delay[m.findCell("NAND2_X1")] = std::numeric_limits<double>::infinity(); rejects(m, model);
        model = f.model; model.required_time[eco::kInvalid] = 1.0; rejects(m, model);
        m.connect("u1/A", "n2"); rejects(m, f.model); // Bypass ECO validation to test graph cycle rejection.
        m.connect("u1/A", "n_in1");
        m.defCell({"DFF", "dff", {"D", "CK"}, {"Q"}, true});
        m.makeInst("ff", "DFF"); rejects(m, f.model);
        eco::MockBackend empty;
        auto result = integration::recalculate(empty, {});
        check(!sta::worst_path(result.mapped.graph, result.timing), "empty design invented a path");
        std::cout << "PASS: pin mapping, hand timing, incremental swaps/deadlines/undo, structural fallbacks and invalid models\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
