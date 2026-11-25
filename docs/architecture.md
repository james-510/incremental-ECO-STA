# Module layout and ownership

This layout follows `reference/project_dir.rtf`, with `src/sta/` matching
`include/sta/` (the RTF alternatively used `engine/` for the implementation).

| Module | Current state | Intended responsibility |
| --- | --- | --- |
| common | Shared stable 32-bit design IDs | Types used by parser, ECO, timing adapters |
| parser/netlist | Not implemented here | Rosstin: parse/link design, library and supported constraints |
| graph | Fixed-delay records only | Dongran: build pin-level graph, adjacency, levelization |
| sta | Full scalar fixed-delay propagation | Dongran: delay/slew calculation, full and incremental timing |
| paths | Top-1 reconstruction | Dongran: define and implement top-K |
| eco | Imported manager | James: design edits, validation, undo, dirty seeds |
| app | Fixture-only demo | Future thin CLI that joins the production modules |
| tests/eco | Mock design + loaders + RefTimer | Integration scaffolding; not production parser/timing |

Current `sta::NodeId`/`ArcId` are graph indices. `common::PinId` and other shared
IDs identify design objects; the graph builder must maintain an explicit mapping.
Do not assume a graph node index equals an ECO pin ID. Existing `eco::PinID`
names are compatibility aliases to common IDs.

Design objects are shared between an editable ECO backend and read-only timing
views. Timing owns its graph/cache/results, not the design or library. Library
tables should be shared across cell instances. Deleted objects need tombstones
or another explicit stable-ID policy while timing state and undo records exist.

Next integration milestone:

1. Extract/share the hand-built mock fixture already in `test_eco_manager.cpp`.
2. Define read-only design/library/constraint views with Rosstin and James.
3. Build a pin-level timing graph with documented fixture delays/boundaries.
4. Run full timing, one cell swap, graph/delay synchronization, full timing again.
5. Implement incremental updates and compare all supported state against full
   recomputation. A structural-edit full-recompute fallback must be reported.

Later files belong in `include/parser/`, `include/netlist/`, `src/parser/`,
`src/netlist/`, `src/graph/`, and `include/sta/delay_calc.hpp` when there is actual
implementation to put there. There is no production parser or incremental engine
hidden behind the current demo.
