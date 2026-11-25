# Planned experiments

Measure full versus incremental timing for the same post-ECO design. Record
design size, live pins/arcs, ECO type, dirty seeds, evaluated/changed pins,
fallbacks, wall time and memory, with fixed random seeds and hardware details.
Validate correctness before timing optimized builds. Keep generated CSVs and
plots under ignored `results/`.

The existing synthetic ECO demo has useful counters but is not a production
engine performance result. Multithreading is a later, separate experiment.
