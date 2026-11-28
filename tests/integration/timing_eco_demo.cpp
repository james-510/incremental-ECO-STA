#include <iomanip>
#include <iostream>
#include "eco/eco_manager.hpp"
#include "shared_fixture.hpp"

static void report(const char* label, const integration::Snapshot& snapshot) {
    const auto path = sta::worst_path(snapshot.mapped.graph, snapshot.timing);
    std::cout << label << ": ";
    if (!path) throw std::runtime_error("fixture unexpectedly has no constrained path");
    std::cout << "arrival=" << path->arrival << " required=" << path->required
              << " slack=" << path->slack << " ns\n  path: ";
    for (std::size_t i = 0; i < path->nodes.size(); ++i) {
        if (i) std::cout << " -> ";
        std::cout << snapshot.mapped.graph.nodes[path->nodes[i]].name;
    }
    std::cout << '\n';
}

int main() {
    try {
        auto fixture = integration::make_fixture();
        auto& design = fixture.design;
        eco::ECOManager edits(design);
        const auto original_design = design.snapshot();
        integration::MockTimingSession timer(design, fixture.model);
        const auto before = timer.snapshot();
        std::cout << std::fixed << std::setprecision(3)
                  << "model=fixed_delay_fixture; initial=full; adapter_sync=full_graph_scan\n";
        report("before", before);
        const auto edit = edits.swapCell("u2", "NAND2_X2");
        if (!edit.ok()) throw std::runtime_error(edit.message);
        const auto seeds = edits.takeSeeds();
        std::cout << "edit=swap_cell u2 NAND2_X2; arrival_seeds=" << seeds.arrival.size()
                  << "; required_seeds=" << seeds.requirement.size()
                  << "; structural=" << std::boolalpha << seeds.structural << '\n';
        const auto stats = timer.update(seeds);
        const auto after = timer.snapshot();
        std::cout << "mode=" << (stats.full_recompute ? "full_rebuild_fallback" : "incremental")
                  << "; arrival_evaluated=" << stats.visited_arrival
                  << "; required_evaluated=" << stats.visited_required
                  << "; live_pins=" << after.mapped.graph.nodes.size() << '\n';
        report("after", after);
        const bool full_match = integration::same_timing(after, integration::recalculate(design, fixture.model));
        std::cout << "check_vs_full=" << (full_match ? "MATCH" : "MISMATCH") << '\n';
        if (!full_match) return 1;
        edits.undoAll();
        const auto undo_seeds = edits.takeSeeds();
        const auto undo_stats = timer.update(undo_seeds);
        std::cout << "undo_mode=" << (undo_stats.full_recompute ? "full_rebuild_fallback" : "incremental") << '\n';
        const auto restored = timer.snapshot();
        const bool match = design.snapshot() == original_design && integration::same_timing(before, restored);
        std::cout << "undo_design_and_timing=" << (match ? "MATCH" : "MISMATCH") << '\n';
        return match ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
