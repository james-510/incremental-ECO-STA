# incremental-ECO-STA

C++17 static timing analysis project with ECO edits and incremental timing.
The repository currently contains two independently runnable development pieces:

- A serial fixed-delay DAG engine: full arrival/required/slack calculation and
  top-1 worst-path reconstruction using hand-built graphs.
- The imported ECO manager and its tests: edits, undo, dirty-pin seeds, mock
  netlists, temporary file loaders, and a synthetic full/incremental timer.

The real parser-to-design contract, NLDM timing engine, pin-to-graph adapter,
incremental timing in that engine, and top-K enumeration are not implemented yet.
The two pieces are not yet connected. `RefTimer` is test infrastructure, not the
production timing engine or an OpenSTA numerical equivalent.

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

## Layout

```text
include/
  common/ids.hpp                 shared design IDs
  graph/timing_graph.hpp         fixed-delay graph records
  sta/timing_engine.hpp          full timing API
  paths/path_enumerator.hpp      top-1 path API; top-K is future work
  eco/                          ECO interfaces
src/
  sta/                          full timing implementation
  paths/                        path reconstruction
  eco/                          imported ECO implementation
  app/main.cpp                  hand-built graph demo
tests/
  unit/                         timing unit tests
  fixtures/                     sample graphs and expected outputs
  eco/                          ECO tests, mocks, synthetic timer, demo
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
