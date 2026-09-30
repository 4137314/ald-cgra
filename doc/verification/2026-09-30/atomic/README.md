# Local atomic commits

The files were staged in cohesive groups without discarding working-tree
content or changing the author's historical commits. Each archived tree in
`build/`, `convolution/`, `transport/` and `cli/` was extracted with `git archive`
and tested in an isolated directory before commit. Manifests identify the
actual Git tree and source SHA256 values, commands and outcomes. CLI then had
two trailing blank lines removed; `cli/format-only.json` records the comparison
that confirms no other difference from its tested tree.

- `00e44e2`: compiler/build signatures, jobserver and 27 build checks.
- `3754afd`: implicit convolution, bounded workspace and library regressions.
- `bff5c57`: transaction deadlines, diagnostics, numeric bounds and PTY service.
- `df3504b`: modular CLI and command/shell contracts; full 1323-check suite.
- `7f2340c`: real kernel benchmarks, profiling, metadata and sweep artifacts.

The benchmark and formal commits are additionally covered by the separately
retained whole-tree gates under `../benchmark/` and the formal archives. The
Nix source snapshots retain the exact bytes used before staging, not fabricated
claims that a later commit hash was already tested. No push/release/hardware
programming was performed.
