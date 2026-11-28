# Fixed-delay incremental timing

`include/sta/incremental_timing.hpp` exposes a serial stateful engine for the
existing scalar DAG model. `analyze_full` remains unchanged as its comparison
baseline. This is not yet NLDM, sequential or multithreaded analysis.

```cpp
sta::IncrementalTiming timer(graph); // owns graph; initial full calculation
sta::TimingUpdate changes;
changes.arc_delays.push_back({arc_id, new_delay});
changes.required_times.push_back({sink_id, new_deadline}); // nullopt clears it
auto stats = timer.update(changes);
auto path = sta::worst_path(timer.graph(), timer.result());
```

The getters are borrowed const references, not snapshots. Copy them to retain
an earlier result across updates. All delay/boundary edits go through `update`.
Connectivity changes require `reset(new_graph)`, which validates and fully
recomputes. The update API cannot mutate topology.

## Scheduling and pruning

The engine retains incoming/outgoing arc lists and topological rank. A changed
delay schedules the destination for arrival calculation and the source for
required-time calculation. Source-arrival edits schedule a forward seed;
endpoint-deadline edits schedule a backward seed. External invalidators can add
extra seeds, but explicit changes always schedule their own dependencies.

Forward work is ordered by increasing rank, backward work by decreasing rank.
Each scheduled pin gathers all relevant neighbor candidates and recomputes its
max/min, then schedules further dependents only if its numeric value changes.
This handles increases, decreases and transitions to/from missing values.
Changed delay can affect upstream required times even when the forward winner
does not change.

Tied arrivals select the smaller ArcId, matching full timing. Predecessor changes
are retained even when the arrival is unchanged. Numeric successors need not be
revisited for a predecessor-only change: worst-path reconstruction follows the
updated chain on demand. Slack is recomputed where arrival or required changed.
Exact comparisons govern pruning; tolerances are used for external comparisons,
not to discard real differences during propagation.

Core update work covers explicit edits, seeds, visited adjacency and priority
queue operations. Queue-membership flags are retained and cleared as nodes are
popped; an update does not clear/copy every result vector. Graph/cache storage
is O(V+E). Worst cases can still require effectively full propagation.

## Counters

Seeds are starting points, not guaranteed value changes or an exhaustive list
of affected pins. The mock NAND swap supplies 3 arrival and 5 required pin seeds.
The adapter translates stable pin IDs to graph node IDs. Explicit delay changes
also schedule both dependency ends. In this fixture propagation evaluates 4
arrival pins and 6 required pins out of 11, then matches full timing.

`UpdateStats` distinguishes visited nodes, changed values, changed predecessors
and recomputed slacks. `full_recompute=true` identifies reset/fallback; full-pass
visited counts include all graph nodes for each pass. Full reset does not count
value changes relative to the discarded cache.

## ECO adapter limits

`MockTimingSession` in `tests/integration/mock_timing_adapter.hpp` borrows the
design and explicit model. Its snapshot returns owned graph/mapping/results.
It still builds/scans a candidate graph on every update to find delay, boundary
and topology changes. That synchronization is not incremental; mock backend
queries may additionally perform linear scans. Fewer propagated pins do not
establish an end-to-end runtime speedup.

Swaps and explicit model boundary changes use the incremental core. Structural
edits, including a structural flag with empty seed lists, use full reset. The
adapter detects topology changes even if a caller omitted the flag. Undo
consumes its own dirty seeds and takes the same update path.

A clock-period edit does not automatically define this fixture's deadlines.
The caller must translate constraints into the explicit boundary map; integration
tests do so explicitly. Actual Liberty delay/slew, rise/fall and sequential
analysis require additional state and dependency rules.

Invalid IDs, nonfinite edits and unsupported boundary locations are rejected
before mutation. An overflow/allocation failure during propagation invalidates
the cache: result access and further updates reject it until successful reset.
Partial results are never exposed as valid timing. This is not transaction
rollback or ECO undo.

## Verification

Unit cases cover increases/decreases, reconvergence, predecessor-only ties,
parallel arcs, unchanged seeds/no-ops, missing boundaries, negative slack,
non-winning-arc required-time changes, invalid edits and overflow recovery.
100 seeded small DAGs receive 30 edit batches each; after every batch, all
arrival/required/slack values, predecessor arcs and worst paths are compared
with a fresh `analyze_full` result. Integration tests exercise actual ECO swaps,
undo, structural fallbacks and explicit boundary updates.
