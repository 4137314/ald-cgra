# Array and UART proofs — 29 September 2026

The complete accepted run `archive/full-20260929T100948Z-70426/manifest.json`
contains **23 PROVED tasks and six expected FAIL controls**. The accepted
cover run `archive/cover-20260929T100950Z-70509/manifest.json` contains the
universal PE obligation (PROVED) and **ten BOUNDED_PASS cover tasks**. Covers
show reachability within 32 cycles, or 80 for the reduced UART instances.
All acceptance flags are true; expected counterexamples retain result FAIL.

See `hw/formal/README.md` for exact clocks, initialization, input domains,
parameters, assumptions and composition. The production PE arithmetic remains
16-bit with all opcodes/selectors. Arrays larger than 1x1 use a PE guarantee
proved on production RTL in the same run, and the actual imported array wiring.
This is an assume-guarantee result, not a monolithic proof of the full netlist.
TX and RX have induction proofs for CLKS_PER_BIT=3 and production value 868.

`archive/index.json` hashes retained files. Each task retains the executed
VHDL, effective PSL, SV harness, SBY configuration, logs, result and traces.
Large generated netlists/SMT files are omitted from the archive; their hashes,
commands and pinned toolchain are in the original results for regeneration.
Original evidence files are copied without rewriting timestamps or outcomes.

Failures retained alongside the final results:

- `fast-20260929T095542Z-31958`: monolithic 2x3 array TIMEOUT, 90 seconds.
- `full-20260929T100345Z-56098`: unguarded previous sample in the initial RX
  monitor gives a reachable FAIL; production UART induction also needs lemmas.
- `full-20260929T100507Z-58992`: three proofs close; production RX remains
  UNKNOWN until the asserted partial-word invariant is complete.
- `full-20260929T100545Z-60299`: array mutation BOUNDED_PASS at depth 4 is an
  acceptance failure. Depth 6 reaches the faulty north route in the final run.

The older interrupted `fast-20260929T095250Z-2` did not complete its second
process during an environment restart. It is not counted as a solver timeout
or a successful run. No production RTL was changed by these monitors/mutations.

The independent digital UART GHDL tests and their source hashes are in
`../uart-ghdl/`: divisors 16 and 868, each with 77 data/fault checks plus
valid-pulse assertions. They vary phase and bit period (±2%), and include
adjacent frames, glitch, bad stop, break, recovery and reset. No analog or
metastability guarantee follows. EXEC response and conditional liveness remain
open under F03; block processing remains open under B01–B03.
