# Module layout and ownership

This layout follows `reference/project_dir.rtf`, with `src/sta/` matching
`include/sta/` (the RTF alternatively used `engine/` for the implementation).

| Module | Current state | Intended responsibility |
| --- | --- | --- |
| common | Shared stable 32-bit design IDs | Types used by parser, ECO, timing adapters |
| parser/netlist | Not implemented here | Rosstin: parse/link design, library and supported constraints |
| graph | Fixed-delay records; test-only mock builder in tests/integration | Dongran: production pin-level graph, adjacency, levelization |
| sta | Full and fixed-topology incremental scalar propagation | Dongran: delay/slew calculation, full and incremental timing |
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

Completed first integration fixture (`tests/integration/`):

- A combinational version of the small ECO mock circuit has explicit per-cell
  delays, source arrivals and output deadlines. It requires no parser.
- `build_pin_graph` maps live pin IDs to compact graph IDs, creates net and cell
  arcs, and calls our existing full engine/path code through `recalculate`.
- The demo swaps a cell, incrementally propagates timing, compares it with fresh
  full timing, and incrementally processes undo. Structural edits/undo use a
  reported full-recompute fallback.
- The core owns topology/adjacency and cached timing. The mock adapter still
  builds/scans a candidate graph to identify deltas; that synchronization is not
  incremental and can dominate runtime.
- Integration tests cover hand-calculated timing, topology changes, path
  switching, missing boundaries and unsupported/invalid inputs.

Next integration milestones:

1. Define production read-only design/library/constraint views with Rosstin and James.
2. Promote/generalize the mock adapter against that agreed contract, including
   real timing arcs and explicit sequential boundary semantics.
3. Add actual library delay/slew calculation and compare aligned inputs to OpenSTA.
4. Extend incremental state/dependencies for slew-dependent timing and optimize
   design synchronization. Fixed-delay updates already match full recomputation;
   structural edits currently have a reported full-recompute fallback.

Later files belong in `include/parser/`, `include/netlist/`, `src/parser/`,
`src/netlist/`, `src/graph/`, and `include/sta/delay_calc.hpp` when there is actual
implementation to put there. The current incremental engine is fixed-delay and
serial; there is no production parser or NLDM model hidden behind the demo.
