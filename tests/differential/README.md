# Differential testing

Current ECO incremental/full checks live in `tests/eco/test_eco_designs.cpp` and
use RefTimer, not the production fixed-delay DAG engine. Existing fixed-delay
graph checks live in `tests/unit/test_timing.cpp` with an enumeration oracle.

Add real timing-engine incremental/full and OpenSTA comparisons here once the
editable design adapter and NLDM model exist. Compare all supported live-pin
state with explicit validity and tolerance rules. The optional OpenSTA report
driver in `scripts/run_opensta.tcl` is preparatory; it is not an automated
numerical comparison. See `docs/opensta-reference.md`.
