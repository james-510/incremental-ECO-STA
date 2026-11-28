# Mock design to our timing engine

`timing_eco_demo` uses James's MockBackend/ECOManager with our actual
`sta::analyze_full` and `sta::worst_path`. It does not use RefTimer.

```sh
make -j4 build/timing_eco_demo build/test_timing_eco
./build/timing_eco_demo
./build/test_timing_eco
```

The CMake build exposes the same two executable names and adds two CTest entries.

The shared fixture is derived from the combinational part of the existing ECO
test circuit. All times below are ns; wire delays are explicitly zero:

```text
in1/P (arrival 0.1) -> u1/INV (delay 0.2) -> u2/NAND2 -> out1/P (deadline 1.0)
in2/P (arrival 0.0) -----------------------> u2/B
in1/P -------------> u3/INV (delay 0.2) -------------> out2/P (deadline 0.8)
```

NAND2_X1 delay is 0.4, NAND2_X2 is 0.25. The longest arrival to out1 is
0.1 + 0.2 + 0.4 = 0.7, slack 0.3. After the swap it is 0.55, slack 0.45.
Out2 has arrival 0.3 and slack 0.5. The demo incrementally processes the swap
and undo, comparing against full timing. The swap evaluates 4 arrival nodes and
6 required nodes out of 11 live pins, including seeds whose values are unchanged.

`mock_timing_adapter.hpp` enumerates live pins, builds explicit bidirectional ID
mappings, creates net edges and all input-to-output edges for each combinational
cell, and supplies boundaries from `FixedModel`. Dead pin slots deliberately
precede live IDs, so graph indices cannot be assumed equal to design pin IDs.
The cell type is re-read during synchronization; delays are refreshed across swaps.

This is an intentionally limited test adapter: no NLDM, rise/fall, slew/load,
sequential checks, clock-based constraints or real arc filtering. Sequential
cells and missing delay data are rejected. No default arrivals or deadlines are
invented. MockTimingSession still builds/scans a candidate graph to identify
updates. Subsequent propagation is incremental; structural changes use a reported
full-recompute fallback.
Keep this under tests until the team agrees on a production design/library API.

The tests check hand-calculated timing and paths, swap and undo, buffer insertion
and undo, reconnect with a critical-endpoint change, missing boundaries, sparse
IDs, cycles, invalid models, sequential rejection and an empty design. Session
checks additionally exercise incremental swaps/undo, explicit deadline/source
changes, pruning, batched edits and structural fallbacks. Standalone core tests
check 3000 seeded edit batches against full timing. These tests do
not claim that identical repeated full recomputations independently validate
each other; the numerical expectations come from the explicit fixture.
