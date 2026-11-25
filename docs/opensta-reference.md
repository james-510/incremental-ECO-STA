# Optional OpenSTA reference workflow

Install a runnable OpenSTA soon so input conventions and tool setup are resolved
before the final report. It is an external correctness reference, not a required
build dependency of our C++17 project. A working existing binary is sufficient;
build from source if one is not available. This reorganization does not install
OpenSTA or a real library, and the scripts have not yet been run on OpenSTA.

Use [the upstream repository/build instructions](https://github.com/parallaxsw/OpenSTA)
for the version you select. The current upstream macOS instructions use Homebrew
dependencies and warn against Apple's bundled Tcl/Flex/Bison versions. CUDD and
other dependencies must be configured as described there. Do not assume an
unverified `brew install opensta` formula exists.

Keep its checkout in an ignored directory, or outside this repository:

```sh
mkdir -p external
git clone https://github.com/parallaxsw/OpenSTA.git external/OpenSTA
# Follow external/OpenSTA/README.md for the selected revision's prerequisites.
# After prerequisites, with CUDD installed at the path you supply:
cmake -S external/OpenSTA -B external/OpenSTA/build \
  -DCMAKE_BUILD_TYPE=Release -DCUDD_DIR=/absolute/path/to/cudd
cmake --build external/OpenSTA/build --parallel 4
mkdir -p results
git -C external/OpenSTA rev-parse HEAD > results/opensta-commit.txt
```

This is a setup recipe, not a record of commands already executed. Record the
exact OpenSTA commit, actual library file/corner/checksum, compiler and input
files when reporting comparisons.

## A before/after recalculation sample

Obtain a real SKY130 HD NLDM library containing the NAND2 cells named by c17 and
both `sky130_fd_sc_hd__nand2_1` and `_2`. `data/lib/mini_sky130.lib` deliberately
has no timing tables. Keep a downloaded large library under ignored `external/`.

From the repository root, using the executable path above:

```sh
OPENSTA_LIB=/absolute/path/to/real_sky130.lib \
  external/OpenSTA/build/sta -exit scripts/run_opensta.tcl

OPENSTA_LIB=/absolute/path/to/real_sky130.lib \
  OPENSTA_ECO="$PWD/data/eco/c17_swap_reference.tcl" \
  external/OpenSTA/build/sta -exit scripts/run_opensta.tcl
```

Use `sta` instead of the full executable path if already on PATH. The script
loads Liberty and c17, sets command units to ns/pF, reads the supplied virtual
clock SDC, and prints max-delay paths. With `OPENSTA_ECO`, it replaces g16 with
the larger NAND2 and reports again. OpenSTA recalculates dependent timing on
demand when the report queries it; no custom `recalc` command is necessary.
The script conditionally defines a scene on scene-aware OpenSTA versions.

`c17_swap_reference.tcl` is a single-swap OpenSTA equivalent, not a translator
for our project's entire ECO command language. It does not perform the buffer
and period changes in `c17_edits.txt`. The SDC uses explicit input transition and
output load, so its assumptions also differ from RefTimer's synthetic model.

Commands and semantics were checked against the upstream
[command reference](https://opensta.readthedocs.io/en/latest/Commands/) and
[examples](https://github.com/parallaxsw/OpenSTA/blob/master/doc/Examples.md).
OpenSTA integration remains unverified until the binary and real library are
available; successful Tcl syntax checking is not a numerical validation.

## Three different correctness comparisons

1. Fixed-delay DAG results versus hand calculations/path enumeration: available.
2. ECO RefTimer incremental versus its full recalculation: available.
3. Our real NLDM timing engine versus OpenSTA on equivalent inputs: future work.

For the third comparison, align library corner, timing arcs/unate behavior,
rise/fall slew, units, clocks, constraints, output loads and zero-wire-parasitic
assumptions. Export the same post-ECO design to each engine. Compare per-pin
state/endpoints with documented absolute/relative tolerances and separately
validate path semantics. Agreement of only worst slack is insufficient.

A full-versus-incremental comparison can share our delay implementation and thus
miss a common delay-model bug. OpenSTA provides the independent timing reference.
Do not present RefTimer's synthetic numbers as an OpenSTA match.
