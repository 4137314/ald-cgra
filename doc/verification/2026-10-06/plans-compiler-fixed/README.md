# Reusable plans — compiler verification after PTY fixture repair

Actual Ubuntu 24.04 container executions, identified by immutable image ID in
`inputs.json`. Inputs were copied once and mounted read-only; `runner.py` copies
them into a fresh build tree for each compiler. Source hashes include the test
fixture repair, public plan API and installed-client regression.

| Compiler | Release | ASan/UBSan/LSan | Analyzer |
|---|---:|---:|---|
| GCC 13.3 | 2295 checks PASS | 2295 checks PASS | library and CLI PASS |
| Clang 18.1.3 | 2295 checks PASS | 2295 checks PASS | not requested: GCC-specific gate |

Counts per profile are 311 library + 197 transport + 406 kernels + 11 injected
faults + 874 plans + 253 DSL + 46 CLI + 58 contract + 139 benchmark. The container
file-descriptor limit causes an explicit skip of the five high-descriptor
checks; the local 5 October runs performed 2300 checks including those five.
LSan remains enabled. These counts are checks, not coverage percentages.

`../plans-compiler/` retains the first failed GCC run and successful Clang run.
The PTY short-read defect, reproduction and 100 successful repetitions after
repair are documented in `../transport-fragment/`. No transport retry or wire
behavior was changed to make the test pass.
