# Nix checks on the consolidated software and EXEC proof sources

Actual `nix build --no-link --json .#checks.x86_64-linux.formal-fast
.#checks.x86_64-linux.software` completed with exit 0. `results.json` records the
output/derivation store paths. `collect.py` was executed to retain derivations,
source store path/hashes, software logs and the formal manifest. Full formal
models/logs are also in the immutable output store path.

- Software: **2300 checks PASS**, including all 202 transport/high-descriptor
  checks and 874 reusable-plan checks.
- Installation **12**, compilation database **187**, incremental build **27**:
  all PASS. GCC library/CLI analyzers and fallback without readline also PASS.
- formal-fast: **10 PROVED + one expected FAIL**, negative witness retained.

These are new executed derivations on x86_64 Linux, not configuration-only CI
claims. Compiler-matrix sanitizer/LSan executions are separate evidence in
`../plans-compiler-fixed/`. No remote Actions run or publication is claimed.
