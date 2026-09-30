# Transport, numeric contracts and configurable PTY (C07/C08)

`release-final/`, `asan-final/`, `analyzer/` and `ghdl/` contain executed gate
logs and manifests: command, exit status, source hashes, commit/dirty state
and duration. All returned 0 and their source sets remained unchanged during
execution. The software suites each contain **1323 checks**, not a coverage
percentage; ASan/UBSan ran with LSan enabled. GHDL regenerated the differential
vectors from the new C model and passed the full testbench set.

## Changes and contracts

- Additive `cgra_open_ex`, transaction timeout and copied diagnostics; existing
  ABI structs and reply-only timeout semantics remain. Serial writes use
  nonblocking descriptors, monotonic polling and one shared budget over NACK
  retries. An uncertain transfer still stops the session without recovery bytes.
- Tests exercise actual PTY backpressure and EINTR, plus wrapped partial writes
  and EAGAIN. Diagnostics identify WRITE/READ/VALIDATE, saved errno, command,
  attempt and cumulative bytes; copies remain valid after close and successful
  calls retain the preceding error until explicitly cleared.
- Close-on-exec is checked both by descriptor flags and a real exec. Optional
  TIOCEXCL is checked against a second open and release on close; it is not a
  lock against existing descriptors or privileged bypass.
- Configurable PTY service supports every legacy geometry and v2/v3, with a
  borrowed cancellation descriptor. Functional tests use 2x3 for both versions,
  incomplete frames, blocked TX and 100 consecutive ID requests. The latter
  previously exceeded the model output FIFO; responses are now drained after
  each input byte. CLI `emulate --rows/--cols/--protocol` stops cleanly on
  SIGINT/SIGTERM. This service does not simulate the RTL watchdog.
- Model bit-vector arithmetic now uses unsigned intermediates, explicit sign
  extension and in-range signed conversion. Boundary/shift tests use independent
  floor-division and modular references. Vector kernels are in `src/vector.c`,
  separate from transport/protocol.

The first extended server test exposed a **test setup race**: a PTY's first
EAGAIN can clear as the kernel moves queued bytes toward the unread master.
`server-first.txt` is retained; the corrected setup waits for stable real
backpressure before injecting signals. `server-stable.txt` and final gates
pass without changing the production timeout criterion.

## Additional compiler/distribution verification

The following runs are in `../2026-09-29/` relative to the dated directory:
`gcc13-final/` (Ubuntu GCC 13.3, software and both analyzers), `clang/`
(Clang 22.1.8, software), `packaging/` (12 installation, 179 compilation database,
27 incremental build checks), and `documents/` (PDF and man/Info). All pass.
The report's changed conclusion on page 8 was rendered and visually inspected;
the complete LaTeX log has no undefined references or overfull boxes.

`gcc13-sandbox/` retains the failed Docker socket permission attempt, not a
software failure; the authorized execution in `gcc13-final/` is separate.
No new Vivado or physical board result belongs to these software changes.
