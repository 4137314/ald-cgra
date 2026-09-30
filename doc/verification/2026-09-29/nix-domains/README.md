# Executed Nix domains — 29 September 2026

All five requested derivations completed with exit 0: installed host package,
software (including 1323 checks, installation/compdb/build, both GCC analyzers
and the no-readline CLI), GHDL/Tcl, eleven-task formal-fast and report PDF.
The workflow is split into software GCC13/Clang18, HDL, formal, documents and
Nix jobs. This is local execution of the pinned derivations, not a claim that
GitHub Actions ran on an unpushed working tree.

`manifest.json` records the exact immutable Nix source store path and hashes;
`software-derivation.json` identifies that input and actual build commands.
`build.txt` is the unmodified combined output; individual software/HDL logs and
the formal task manifest are copied from the successful outputs. Nix 2.35.2,
nixpkgs as fixed by flake.lock. The first attempt failed to write the fetcher
lock in the sandbox; its separate log is retained. The authorized run then
completed without weakening any gate.

These inputs predate the new CLI benchmark/profile work of 30 September. Do
not apply their source hashes or check totals to those later changes.
