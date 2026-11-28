# incremental-ECO-STA

C++17 static timing analysis project with ECO edits and incremental timing.
The repository currently contains two independently runnable development pieces:

- A serial fixed-delay DAG engine: full arrival/required/slack calculation and
  top-1 worst-path reconstruction using hand-built graphs.
- The imported ECO manager and its tests: edits, undo, dirty-pin seeds, mock
  netlists, temporary file loaders, and a synthetic full/incremental timer.

The real parser-to-design contract, NLDM timing engine, production pin-to-graph
adapter, and top-K enumeration are not implemented yet. A stateful fixed-delay
engine supports incremental delay/boundary updates on a fixed topology. A
test-only mock adapter connects it to ECO with full recomputation for structural
edits. `RefTimer`
is separate test infrastructure, not the production timing engine or an OpenSTA
numerical equivalent.

## Build and test

Requires a C++17 compiler and Make. From the repository root:

```sh
make -j4
make test
./build/sta_demo budget_compare
./build/sta_demo violations
```

With CMake 3.20 or newer:

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cmake --parallel 4
ctest --test-dir build-cmake --output-on-failure
```

`sh scripts/run_all.sh` selects CMake if available, otherwise Make. Use separate
`build/` and `build-cmake/` directories for the two build systems. Output from
builds, external tools, local archives, and experiments is ignored by Git.

## ECO recalculation example

```sh
./build/eco_demo --lib data/lib/mini_sky130.lib \
  --netlist data/netlists/c17.v --eco data/eco/c17_edits.txt \
  --period 1.0 --undo
```

This performs full timing, applies the script, collects dirty seeds, performs an
incremental update, compares all live-pin arrivals/required times with fresh
full timing, and verifies undo. Its synthetic timer does not read Liberty delay
tables. The mini library contains pin metadata only. See
[the recalculation contract](docs/timing-eco-contract.md) and
[OpenSTA reference instructions](docs/opensta-reference.md).

## Our engine connected to ECO (fixed-delay fixture)

```sh
./build/timing_eco_demo
# CMake build: ./build-cmake/timing_eco_demo
```

This uses a combinational version of the small ECO mock circuit, converts live
pins/nets and cell connections to a timing graph, calls our actual `analyze_full`
and `worst_path`, swaps u2 to a different fixture delay, incrementally updates,
then undoes and checks both design and timing restoration. The snapshots own
their graph and values. It does not call RefTimer or a file parser. The demo
prints `mode=incremental`, evaluated-pin counts, and `check_vs_full=MATCH`.
The mock adapter still builds/scans a candidate graph to identify changes; only
the subsequent timing propagation is incremental. Reduced propagation work is
not an end-to-end speedup claim.

Expected worst slack: 0.300 ns before the swap, 0.450 ns after, followed by
`undo_design_and_timing=MATCH`. Tests also exercise buffer insertion, reconnect,
critical-endpoint switching, missing boundaries, sparse live IDs and rejection
of cycles, invalid models and unsupported sequential cells. Details live in
`tests/integration/`; this test adapter does not freeze the parser team's API.
Structural edits/undo use an explicit full-recompute fallback. See
[incremental timing](docs/incremental-timing.md) for the API, counters and limits.

## Layout

```text
include/
  common/ids.hpp                 shared design IDs
  graph/timing_graph.hpp         fixed-delay graph records
  sta/                          full and fixed-topology incremental timing APIs
  paths/path_enumerator.hpp      top-1 path API; top-K is future work
  eco/                          ECO interfaces
src/
  sta/                          full and incremental timing implementations
  paths/                        path reconstruction
  eco/                          imported ECO implementation
  app/main.cpp                  hand-built graph demo
tests/
  unit/                         timing unit tests
  fixtures/                     sample graphs and expected outputs
  eco/                          ECO tests, mocks, synthetic timer, demo
  integration/                  mock design -> our engine -> ECO updates
  differential/                 planned real-engine comparison workflow
data/
  lib/  netlists/  sdc/  eco/    inputs, constraints, edit scripts
scripts/                        test runner and optional OpenSTA driver
bench/                          experiment plan
docs/                           contracts, Git workflow, provenance, RTF reference
```

Parser/netlist modules, a graph builder, delay calculation, and a real netlist
CLI will be added when implemented. No empty API stubs imply those features
exist. See [architecture](docs/architecture.md).

## Git workflow

Use a local feature branch, review/stage intended source files, commit, then push
the branch and open a PR. See [the exact commands](docs/git-workflow.md).
The ECO snapshot was imported from an extracted download, so it does not bring
its original Git history; coordinate the first integration with the ECO owner.
