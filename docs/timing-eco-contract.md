# ECO to timing: existing calls and proposed production contract

## Existing runnable example

`tests/eco/eco_demo.cpp` already contains this sequence (using its `MockBackend`):

```cpp
eco::ECOManager edits(design);
eco::RefTimer timer(design);
timer.fullUpdate();

auto result = edits.swapCell("g16", "sky130_fd_sc_hd__nand2_2");
if (!result.ok()) throw std::runtime_error(result.message);
auto seeds = edits.takeSeeds();
timer.incrementalUpdate(seeds.arrival, seeds.requirement, seeds.structural);

eco::RefTimer fresh(design);
fresh.fullUpdate();
std::string why;
bool match = timer.sameAs(fresh, &why);
```

These timer calls exist today. The demo applies an edit script instead of just a
single swap, then also optionally undoes and verifies restoration. A separate
engine instance on the mutated design supplies full recomputation. The original
timing snapshot must own its values if it is used to check undo.

`RefTimer` is a toy model: scalar timing, synthetic load-dependent cell delay,
fixed sequential numbers, zero net delay, and test-specific unconstrained and
floating-pin conventions. It is useful for exercising ECO dirty seeds, but its
numbers cannot be compared directly to NLDM/OpenSTA output.

## Proposed production API (not implemented)

```cpp
class TimingEngine {
public:
    void fullUpdate();
    UpdateStats update(const eco::DirtySeeds& seeds);
    TimingSnapshot snapshot() const; // owned values, design revision included
    std::vector<TimingPath> worstPaths(std::size_t k) const;
};
```

The engine is constructed from agreed read-only design, library, and constraint
views. ECO mutates the same underlying design first. No CSV handoff is needed.
`update` synchronizes changed cell arcs, connectivity, load and boundaries before
propagating. `structural` requires graph/topological-order synchronization; an
initial implementation may explicitly fall back to full rebuilding/recomputation.
The current flag + pin lists may require expansion if that synchronization needs
more detailed edit information.

Return per-pin rise/fall arrival, required time, slew and slack, plus predecessor
arc/transition information, endpoint slack summaries and paths. Keep missing
values explicit. Statistics should distinguish pins evaluated from pins whose
state changed, and expose whether a full-recompute fallback was used.

Important semantics to agree before implementation:

- Times in ns, capacitance in pF; normalize library data at the adapter boundary.
- Stable IDs, alive-object enumeration, revision tracking and ownership.
- Launch/capture behavior, virtual clocks, I/O constraints and unsupported SDC.
- Delay/slew/required dependencies: changed delay can require backward work even
  if a forward maximum arrival does not change; arrival alone is not a sufficient
  pruning key once slew-dependent delay is implemented.
- `DirtySeeds::empty()` currently ignores `structural`; inspect that flag too.
- `takeSeeds()` consumes pending seeds. Reuse the returned value if needed;
  calling it a second time does not retrieve the same changes.
- `commit()` clears undo history; it does not recalculate timing or consume seeds.
- Compare validity states and all supported per-pin values, not just worst slack.
  Define numerical tolerance and deterministic path ties. `RefTimer::sameAs`
  currently uses exact comparison for its own synthetic model.

Our current implemented API remains `sta::analyze_full(graph)` and
`sta::worst_path(graph, result)` in `include/sta/` and `include/paths/`.
It has no editable-netlist adapter or incremental entry point yet.
