# STA starter: graph, fixed-delay timing, and top-1 path

This is an educational C++17 starter for the second role of a three-person STA
project. It proposes an engine-side interface, supplies hand-built inputs and
independently specified expected outputs, and provides a small working serial
implementation you can read, modify, or replace. It does not use another timing
engine to generate its answers.

## Recommended development order

1. Build the graph representation, forward/reverse adjacency as needed,
   topological order, and cycle detection.
2. Initialize input arrivals and endpoint deadlines, propagate arrivals forward
   and required times backward, then compute slack.
3. Reconstruct a top-1 path using saved predecessor arc IDs.
4. Connect the graph builder to parsed design/library/constraint objects.
5. Add rise/fall, slew/load-dependent cell models, and the sequential features
   agreed for your project. The order of steps 4 and 5 can overlap.
6. Define and implement top-K paths, then profile before parallelization.

Implement top-1 alongside the first timing pass: it is a useful debugging aid.
You do not need to finish every graph algorithm before computing any timing.

## Build and run

Requires a C++17 compiler and make; no third-party C++ dependencies.

```sh
make
make test
./demo budget_compare
./demo violations
./demo missing_boundaries
./demo ties
./demo cycle    # Expected error and exit status 1.
```

Without make:

```sh
c++ -std=c++17 -O0 -g -Wall -Wextra -Wpedantic timing.cpp samples.cpp demo.cpp -o demo
c++ -std=c++17 -O0 -g -Wall -Wextra -Wpedantic timing.cpp samples.cpp tests.cpp -o tests
./tests
```

Optional memory/undefined-behavior checking, where the compiler supports it:

```sh
make clean
make CXXFLAGS='-std=c++17 -O0 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer'
make test
```

## Who supplies the input?

The intended final flow is:

- Standard files: gate-level Verilog, Liberty, and the supported SDC subset.
- Teammate 1: parse/link these into shared Design, Library, and Constraints data.
- Your graph builder: convert those objects into a timing graph and boundaries.
- Your engine: compute timing and return results.
- Teammate 3: apply ECO edits and run comparisons/benchmarks through agreed APIs.

The parser does not have to construct the timing graph directly. Splitting the
parsed logical design from your timing graph lets you choose your own graph
layout. In this starter samples.cpp stands in for the data supplied by that
builder. It is not a temporary Verilog or Liberty parser.

Existing EDA tools can produce perfectly valid input files, e.g. a synthesized
gate-level netlist. An OpenSTA timing report is useful as an external comparison
result, but is not a substitute for the graph and timing computation you own.
Your course/project rules determine whether reuse of code or precomputed delay
annotations is allowed; this starter assumes you implement the core yourself.

## Proposed interface and ownership

Read timing.hpp first. The public operations are:

```cpp
TimingResult analyze_full(const TimingGraph& graph);
std::optional<TimingPath> worst_path(const TimingGraph& graph,
                                    const TimingResult& result);
```

Input graph owns vectors of Node and Arc values. IDs are vector indices. The
engine borrows the graph read-only for the call. Results own their vectors and
refer to graph objects by IDs. Pass the unchanged graph to worst_path; a result
must be discarded after any graph or boundary edit. This starter has no graph
version guard and is intended for one-process serial use.

Node fields:

| Field | Meaning |
| --- | --- |
| name | Unique debug label; later use the full hierarchical pin name |
| input_arrival | Arrival at an explicitly initialized graph source, in ns |
| required_time | Absolute latest allowed arrival at a constrained sink, in ns |

Arc fields:

| Field | Meaning |
| --- | --- |
| from, to | Source/destination NodeId |
| delay | Fixed propagation delay in ns |
| kind | Cell or net; informational in this version |

No shared raw pointers are required. Use integer cell/pin/arc IDs in the future
parser contract too, or agree on stable object lifetimes if using pointers.
Your own graph builder should retain mappings between design pins and graph IDs.
IDs remain stable only while the vectors are not deleted from or reordered.
Full recomputation after a mutation is supported; incremental updates are not.

For the eventual Liberty model, keep library tables shared between instances:
a cell arc can carry a library arc ID while each instance arc stores its own
computed delay/slew. Pin capacitance, output load, input slew, transition sense,
and rise/fall tables belong in that expanded contract. Do not freeze this
starter's single scalar delay per arc as the final Liberty interface.

## Struct/class versus a temporary file

Use structs/classes and function calls when the three components are linked
into one C++ executable. Keep the common types in a shared header, with a single
owner for the contract. A struct is suitable for data records; a class can later
encapsulate mutable graph state and update operations.

Files are useful for saved fixtures, inspection, and reproducing failures. They
are the natural boundary if the components are intentionally separate programs
or processes. Do not introduce a file parser simply to connect C++ functions.
This starter uses C++ fixtures and CSV-like debug output to avoid that overhead.

## Timing semantics

This is fixed-delay maximum-arrival analysis on a DAG, with no rise/fall split.

- A[v] = max(A[u] + delay[u,v]) over reachable incoming arcs.
- R[u] = min(R[v] - delay[u,v]) over successors reaching constrained sinks.
- S[v] = R[v] - A[v] when both are known.
- Input arrivals and endpoint deadlines are absolute times on the same origin.
- Only graph sources may have input_arrival; only sinks may have required_time.
- Missing arrival/required values remain std::nullopt (printed as NA).
- Unreachable or unconstrained endpoints are excluded from critical ranking.
- A graph cycle is rejected, even if it is disconnected from initialized inputs.
- Finite negative delays are mathematically supported; no physical model is
  implied. NaN, infinity, invalid IDs, duplicate/empty names, and arithmetic
  overflow are rejected.

The most critical endpoint has the smallest slack, including negative slack.
It is not selected by the smallest absolute distance to zero. Among paths to
one endpoint, the maximum-arrival path has minimum slack in this model.
Different endpoint deadlines mean the globally longest path need not be worst.

Exact arrival ties select the smaller ArcId; endpoint slack ties select the
smaller NodeId. No floating tolerance is used to choose winners, so a small real
timing difference is not discarded. Tests use tolerance for decimal expectations.
Return only one tied worst path here; general top-K requires more state/search.

## Sample 1: different budgets and reconvergence

IDs are a=0, b=1, x=2, z=3, y1=4, y2=5. Source arrivals: a=1, b=0.
Endpoint deadlines: y1=8, y2=4.5. All values in ns.

| ArcId | From | To | Delay |
| --- | --- | --- | ---: |
| 0 | a | x | 2 |
| 1 | b | x | 4 |
| 2 | x | y1 | 3 |
| 3 | a | z | 1 |
| 4 | z | y2 | 2 |

All complete paths can be enumerated by hand:

| Path | Input arrival | Sum of delays | Endpoint arrival | Deadline | Slack |
| --- | ---: | ---: | ---: | ---: | ---: |
| a, x, y1 | 1 | 5 | 6 | 8 | 2 |
| b, x, y1 | 0 | 7 | 7 | 8 | 1 |
| a, z, y2 | 1 | 3 | 4 | 4.5 | 0.5 |

Worst path: a, z, y2. Node timing and predecessor values are specified in
expected/budget_compare.txt. Internal-node slack combines max forward arrival
and min backward required time; it is not the slack of every path through it.

## Other samples

| Fixture | Change/purpose | Expected result |
| --- | --- | --- |
| violations | y1 deadline=6; y2 deadline=3.9 | b,x,y1 wins at -1 ns; a,z,y2 has -0.1 ns |
| missing_boundaries | One unconstrained branch and one branch with no initialized input | No reportable constrained path; NA values preserved |
| ties | Two equal 1 ns paths to one endpoint | Arc 0 wins reproducibly |
| cycle | u to v and v to u | Diagnostic and nonzero exit |

## How to debug your own implementation

1. Check IDs, arc direction, node/arc counts, and topological order first.
2. Compare every node's arrival, required time, slack, and predecessor arc with
   the small expected files. Do not debug only the final WNS or worst path.
3. At a suspect node, print each incoming candidate A[u]+delay and the selected
   winner. For required time, print each outgoing candidate R[v]-delay.
4. Verify every arc obeys the topological order and that all initialized values
   appear only at supported boundaries. Check units at the parser adapter.
5. Trace the chosen path and verify input arrival plus all delays equals its
   endpoint arrival. Check deadline minus arrival equals reported slack.
6. Save the failing graph and boundaries as a tiny reproducible fixture.
7. Once integrating with an external reference, align input files, corner,
   units, wire-delay assumptions, clocks, and supported constraints before
   comparing. Fixed toy delays are not automatically equivalent to real Liberty
   delay calculations in another tool.

tests.cpp checks named expectations and invalid inputs, then checks 150 seeded
small random DAGs against explicit path enumeration, independently of the
topological timing algorithm. This is cheap for tiny fixtures; enumerating all
paths is not appropriate for your production timing computation.

## Limits and next steps

No Verilog/Liberty/SDC readers, real delay tables, slew/load model, setup/hold
checks, clocks, latches, timing exceptions, multiple corners, CPPR, incremental
invalidation, top-K enumeration, or parallel execution are implemented.

For parallel propagation, first add incoming adjacency and make each ready
vertex gather predecessor results and write only its own state. Use level
barriers or a dependency-aware scheduler. The serial arrival loop in timing.cpp
pushes updates to successors and would race if blindly made parallel. Keep the
serial mode for comparison while developing parallel scheduling.
