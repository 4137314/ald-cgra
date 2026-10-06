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
- **PHYS**: [`verification/2026-09-30/synchronizers/`](verification/2026-09-30/synchronizers/)
  actual routed/CDC runs and full/cover proofs including CFG/WR capture guards.
- **CONV**: [`verification/2026-09-28/implicit-conv/`](verification/2026-09-28/implicit-conv/)
  and its linked 50-point before/after measurements.
- **EXEC**: [`verification/2026-10-06/formal-exec/`](verification/2026-10-06/formal-exec/)
  new full/fast/cover executions, response properties and well-founded ranks;
  [`exec-ghdl/`](verification/2026-10-06/exec-ghdl/) replays the byte mutation.
- **PLANS**: [`verification/2026-10-05/plans-release/`](verification/2026-10-05/plans-release/)
  and [`verification/2026-10-06/plans-compiler-fixed/`](verification/2026-10-06/plans-compiler-fixed/):
  874 plan checks; compiler/profile counts and explicit container skips are recorded.
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
| CFG/WR changes precede capture by the required guard | Protocol replay and meaningful changed-data cover | `F_CTRL_CFG_CHANGE_CONTEXT`, `F_CTRL_INPUT_CHANGE_CONTEXT`, `F_CTRL_DATA_CAPTURE_GUARD`: PROVED | PHYS full/cover; no rollback assumption; CE/reset must remain single-cycle |
| Legacy CFG/WR partial writes survive NACK | C model oracle and protocol differential replay | Not an atomic-commit guarantee | HDL; NACK is not rollback |
| Masks, ascending taps, response status/checksum | Protocol replay, kernel UART benches; temporary byte-swap mutation | `F_EXEC_*`: PROVED on enumerated controller instances | EXEC; all mask/data bits symbolic; per-byte input sampling and array hold composition specified in the contract |
| Parser index legality, TX launch and stable handshake data | Controller/array replay under TX busy | `F_CTRL_*` safety invariants: PROVED | FORMAL; no fairness assumptions in current safety proof |
| Watchdog returns parser to idle | Reduced-timeout RTL replay | Counter range/tick/clear and timeout assertions PROVED | EXEC; full instances retain production counter, short covers use reduced timeout |
| Transaction eventually completes under delivery/service obligations | Protocol replay; nonempty full, sparse, empty and NACK covers | Request/RUN/EXEC/TX natural ranking certificates PROVED | EXEC; [conditional completion argument](../hw/formal/completion.md), no separate LTL engine or unconditional deadlock claim |
| Uncertain reply poisons session; no duplicate retry | PTY `test_uncertain_session`, truncated/bad replies, disconnect | No host formal claim | SW; next calls fail DESYNC without sending bytes |
| Whole transaction budget includes TX/backpressure/EINTR | PTY write/read/retry budgets, injected partial writes | No host formal claim | SW; response timeout is separate, multi-command kernels have per-command budgets |
| Diagnostic ownership, CLOEXEC and optional exclusive open | `test_diagnostic_persistence`, real exec/PTY flag checks | No host formal claim | SW, CC; diagnostics are copied values, not borrowed strings |
| Configurable/stoppable PTY and consecutive frames | v2/v3 rectangles, cancellation and 100 consecutive ID requests | Clockless model has no watchdog timing model | SW; cancelled TX backpressure and partial request tested |
| Reusable plans, resident binding, ownership and invalidation | `test_plans`, PTY uncertainty, injected allocation faults and installed external client | No C formal claim | PLANS; v2/v3 and eight shapes, 0/255 steps, stale calls emit no bytes, no implicit rebind after uncertainty |
| Systematic corrupted header/opcode, inserted/lost byte matrix | Existing selected malformed/checksum cases | Arbitrary bytes covered for controller safety only | **Partial** H3/A11: arbitrary-byte safety does not prove resynchronization or functional recovery for every corruption |

## UART and delivery

| Requirement | Executable regression | Formal property / scope | Evidence and limit |
|---|---|---|---|
| TX idle/start/data/stop, bit count and LSB order | `tb_uart`, independent downstream kernel protocol checks | `F-UART-TX-C3/C868`: PROVED | FORMAL; no fairness on start requests, reset may interrupt |
| RX start confirmation, sampled bits, stop rejection, valid pulse | `tb_uart_async`: 12 phase/period combinations, adjacent frames, ±2%, glitch, bad stop, break, reset/recovery | `F-UART-RX-C3/C868`: PROVED digital sample model | HDL: 77 data/fault checks per divisor 16/868; not an analog metastability certification |
| Nonvacuous properties / broken DUT detected | Explicit reset/opcode/timeout/NACK/completion covers and temporary mutations | 11 cover tasks BOUNDED_PASS; seven negative tasks FAIL as required | EXEC; all positive full tasks PROVED. Cover is not universal proof |
| CLI validation before effects; whole-line shell parsing | `test_contract.py`, CLI/output regressions | No formal claim | SW, PKG; quoted negatives, option arity, long lines/tokens/NUL and PTY signals |
| Compiler/flags/optional dependencies invalidate build | `scripts/test_build.py` | No formal claim | PKG; unchanged signature does not rebuild, nonexistent compiler must fail |
| Packaging, compdb, compiler diagnostics | Installation external client, compdb actual flags, GCC analyzer | No formal claim | PKG, CC; GCC13 and Clang22 executed locally, CI compiler matrix is separate evidence |
| Benchmark kernels, CPU statistics, every sweep point retained | CLI public APIs, scalar references, launch-count regression, 40 geometry points | No formal claim for timings | [30 September benchmark evidence](verification/2026-09-30/benchmark/); schema 2 retains samples, provenance and every point |
| CDC attributes, endpoint coverage and routed timing | Actual Vivado 2026.1, 4×4/DIV2/10 ns: WNS +1.862 ns, WHS +0.098 ns; two recognized synchronizers | Formal guards support multicycle premises, exclude CDC/analog/STA | PHYS: source hashes and separate baseline; no board claim |
| Isolated bitstream manifest and publication | 80 Tcl / 31 artifact regressions; real bit run and `check-artifact` PASS | No formal software guarantee | [5 October physical evidence](verification/2026-10-05/physical/); no programming performed |
| Matched five-shape physical comparison | Five actual Vivado STA runs at 10 ns/DIV2, identical source/tool hashes | Not a parametric timing theorem | [5 October sweep](verification/2026-10-05/geometry-physical/); each run retains its own reports and resource counts |
| Final Fmax candidate and period precision | Actual final reroute at 5.859 ns, WNS +0.177/WHS +0.122; 83 Tcl / 31 artifact checks | No claim of global maximum or changed oscillator | [6 October Fmax](verification/2026-10-06/fmax/); first precision-mismatch failure retained |
| Block framing/CRC/dedup and resident dot | 1036 private codec/model checks; encoded-wire dot comparison | RTL/formal block properties NOT_RUN | [6 October private model](verification/2026-10-06/block-model/); B01 partial, B02–B03 pending; no new device capability advertised |

Historical counts and hashes remain in their original directories. Later
source changes require a new run; the links above never imply that old logs
were regenerated from the current working tree.
