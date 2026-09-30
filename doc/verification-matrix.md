# Requirements, tests, properties and evidence

This matrix describes the current verified scope, not code coverage. A passing
example does not prove the entire input domain. Stable requirement names refer
to the public contracts or named monitors in `hw/formal/`; the task tracker is
`TASKS.md`. Formal assumptions and instance lists are in
[`hw/formal/README.md`](../hw/formal/README.md).

Evidence aliases (all paths are relative to this document):

- **SW**: [`verification/2026-09-28/transport/`](verification/2026-09-28/transport/)
  release/ASan manifests and source hashes, 1323 checks each, LSan enabled.
- **CC**: [`verification/2026-09-29/gcc13-final/`](verification/2026-09-29/gcc13-final/)
  and [`clang/`](verification/2026-09-29/clang/): actual compiler executions.
- **HDL**: [`verification/2026-09-29/uart-ghdl/`](verification/2026-09-29/uart-ghdl/)
  full GHDL suite, including the independent UART source.
- **FORMAL**: [`verification/2026-09-29/formal/`](verification/2026-09-29/formal/)
  archived full/cover manifests, effective sources, logs and counterexamples.
- **CONV**: [`verification/2026-09-28/implicit-conv/`](verification/2026-09-28/implicit-conv/)
  and its linked 50-point before/after measurements.
- **PKG**: [`verification/2026-09-29/packaging/`](verification/2026-09-29/packaging/)
  12 installation, 179 compdb and 27 build checks.

## Numeric and spatial contracts

| Requirement | Executable regression | Formal property / scope | Evidence and limit |
|---|---|---|---|
| Reset overrides step/config; hold without step | `tb_pe`, `test_cgra` | `F-PE-SMOKE`, `F-PE-ALU-MUX`: PROVED | HDL, FORMAL; initial reset in PE harness, later inputs unrestricted |
| All 16 opcodes and both 8-way operand muxes | `tb_pe`, C/RTL array differential vectors | `F-PE-ALU-MUX`: PROVED, all symbolic 16-bit inputs | FORMAL; reserved selector 7 yields zero |
| Wrap, MUL/MAC low half, ABS(-32768), signed MIN/MAX | `test_cgra`, `test_kernels`, differential replay | Same universal PE transition | SW, HDL, FORMAL; covers are additional reachability checks |
| Shift count masked to four bits; SHL modulo, arithmetic SHR | Independent scalar boundary cases in `test_cgra` | Same universal PE transition | SW, FORMAL; C uses defined unsigned operations/sign conversion |
| Row-major N/S/E/W wiring, borders and synchronous capture | `tb_array_geometry`, distinct CONST seeds and all neighbor routes | `F-ARRAY-*`: PROVED on eight enumerated shapes | FORMAL; 1x1 monolithic, other shapes compose the separately proved production PE guarantee |
| Config of unrelated PE does not affect this capture | `tb_array_geometry`; symbolic array config | Per-PE array transition assertions | FORMAL; not a theorem for every generic geometry |
| Kernel capacities, aliases, empty inputs, partial chunks | `test_kernels`, `test_kernel_partial_output` | No C formal claim | SW; v2/v3, runtime geometry; aliases remain API-specific |
| Implicit convolution with nonquadratic tile traversal | Independent scalar oracle and allocation-fault checks | No complexity theorem in solver | SW, CONV; full heap removed, byte/transaction/step counts preserved |

## Protocol and transport contracts

| Requirement | Executable regression | Formal property / scope | Evidence and remaining gap |
|---|---|---|---|
| RUN/EXEC captures 0..255; guard and spacing for DIV1/2/3/4 | `gen_protocol_vectors`, `tb_protocol_diff` | `F_CTRL_CAPTURE_*`, pending count and legal-state invariants: PROVED | HDL, FORMAL; 255 request cover reached, 255 completion not claimed from short covers |
| EXEC bad checksum causes no datapath reset/step | C/RTL transcript checksum cases | `F_CTRL_BAD_EXEC`: PROVED, external reset considered | HDL, FORMAL; payload edge-register writes remain permitted |
| Legacy CFG/WR partial writes survive NACK | C model oracle and protocol differential replay | Not an atomic-commit guarantee | HDL; NACK is not rollback |
| Masks, ascending taps, response status/checksum | Protocol replay, kernel UART benches | Full independent EXEC response proof NOT_RUN | HDL; F03 remains open |
| Parser index legality, TX launch and stable handshake data | Controller/array replay under TX busy | `F_CTRL_*` safety invariants: PROVED | FORMAL; no fairness assumptions in current safety proof |
| Watchdog returns parser to idle | Reduced-timeout RTL replay | Timeout next-state assertion PROVED | FORMAL; full instances retain production counter, short covers use reduced timeout |
| Transaction eventually completes | Finite successful protocol replays | Conditional liveness proof NOT_RUN | Requires explicit byte-delivery and TX-progress obligations; no unconditional deadlock claim |
| Uncertain reply poisons session; no duplicate retry | PTY `test_uncertain_session`, truncated/bad replies, disconnect | No host formal claim | SW; next calls fail DESYNC without sending bytes |
| Whole transaction budget includes TX/backpressure/EINTR | PTY write/read/retry budgets, injected partial writes | No host formal claim | SW; response timeout is separate, multi-command kernels have per-command budgets |
| Diagnostic ownership, CLOEXEC and optional exclusive open | `test_diagnostic_persistence`, real exec/PTY flag checks | No host formal claim | SW, CC; diagnostics are copied values, not borrowed strings |
| Configurable/stoppable PTY and consecutive frames | v2/v3 rectangles, cancellation and 100 consecutive ID requests | Clockless model has no watchdog timing model | SW; cancelled TX backpressure and partial request tested |
| Systematic corrupted header/opcode, inserted/lost byte matrix | Existing selected malformed/checksum cases | Arbitrary bytes covered for controller safety only | **Partial** H3/A11: arbitrary-byte safety does not prove resynchronization or functional recovery for every corruption |

## UART and delivery

| Requirement | Executable regression | Formal property / scope | Evidence and limit |
|---|---|---|---|
| TX idle/start/data/stop, bit count and LSB order | `tb_uart`, independent downstream kernel protocol checks | `F-UART-TX-C3/C868`: PROVED | FORMAL; no fairness on start requests, reset may interrupt |
| RX start confirmation, sampled bits, stop rejection, valid pulse | `tb_uart_async`: 12 phase/period combinations, adjacent frames, ±2%, glitch, bad stop, break, reset/recovery | `F-UART-RX-C3/C868`: PROVED digital sample model | HDL: 77 data/fault checks per divisor 16/868; not an analog metastability certification |
| Nonvacuous properties / broken DUT detected | Explicit reset/opcode/timeout/NACK/completion covers and temporary mutations | 10 cover tasks BOUNDED_PASS; six negative tasks FAIL as required | FORMAL; all positive full tasks PROVED. Cover is not universal proof |
| CLI validation before effects; whole-line shell parsing | `test_contract.py`, CLI/output regressions | No formal claim | SW, PKG; quoted negatives, option arity, long lines/tokens/NUL and PTY signals |
| Compiler/flags/optional dependencies invalidate build | `scripts/test_build.py` | No formal claim | PKG; unchanged signature does not rebuild, nonexistent compiler must fail |
| Packaging, compdb, compiler diagnostics | Installation external client, compdb actual flags, GCC analyzer | No formal claim | PKG, CC; GCC13 and Clang22 executed locally, CI compiler matrix is separate evidence |
| Benchmark kernels, CPU statistics, every sweep point retained | CLI public APIs, scalar references, launch-count regression, 40 geometry points | No formal claim for timings | [30 September benchmark evidence](verification/2026-09-30/benchmark/); schema 2 retains samples, provenance and every point |
| CDC attributes, endpoint coverage, routed timing, current bitstream manifest | Historical Vivado only | Formal excludes CDC/analog/STA | A04/A05 remain open; no new Vivado result from these software/formal runs |
| Block framing/CRC/dedup, local memory and sequencer | NOT_RUN | NOT_RUN | B01–B03; no new protocol capability advertised |

Historical counts and hashes remain in their original directories. Later
source changes require a new run; the links above never imply that old logs
were regenerated from the current working tree.
