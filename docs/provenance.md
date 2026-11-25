# Imported material and preserved references

Reorganized on 2026-11-25 from the user's local fixed-delay starter and extracted
`incremental-ECO-STA-eco/` directory. The latter was supplied as the team's ECO
branch download; its exact source Git commit is not present in the extracted
files. No upstream branch history was merged or fetched by this reorganization.

- ECO sources/headers moved to `src/eco/`, `include/eco/`.
- ECO mocks, tests, loaders and demo moved unchanged to `tests/eco/`.
- ECO input data moved unchanged to `data/`.
- The only ECO header adaptation is the shared-ID aliases in `eco_types.hpp`;
  the underlying ID widths and values are unchanged.
- Fixed-delay timing/path functions were separated without algorithm changes.
- Original starter documentation is retained in `archive/starter-guide-original.md`.
  Its old paths/build instructions are historical; use the root README now.
- Original ECO README/build configuration/ignore file are retained in `archive/`.
- The supplied layout reference is retained as `reference/project_dir.rtf`.
- The user's starter ZIP is preserved locally as `archives/Archive17Nov.zip`
  at repository root (ignored), not uploaded with source files.

No OpenSTA source or external library was imported by this change.
