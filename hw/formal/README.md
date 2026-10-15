# Formal RTL contracts

This directory proves selected properties of **production VHDL**. It does not
certify the complete system, C software, CDC metastability, analog UART timing
or place and route. Remaining integration/corruption work and proof limits are tracked in
A11/B03 in `TASKS.md`. The conditional EXEC contract is qualified below.

## Run

From the repository root:

```
nix develop .#formal -c make formal-check-deps
nix develop .#formal -c make formal-fast
nix develop .#formal -c make formal-full
nix develop .#formal -c make formal-cover
```

Standalone Make targets also work when GHDL, Yosys **with the GHDL plugin**,
SBY, Yices and Z3 are on PATH (`make -C hw -f hw.mk formal-fast`). A missing tool
is an error. `formal-smoke` isolates the initial frontend and negative probe.
`python3 hw/formal/run.py --suite fast --out NEW_DIRECTORY` selects a new
output directory; an existing directory is never silently overwritten.

`flake.lock` fixes nixpkgs; `toolchain.json` lists the tested packages:
GHDL 6.0.0, plugin revision `1b97dc7`, Yosys 0.67, SBY 0.61, Yices 2.7.0.
The packaged SBY executable reports `unknown version`; the package version
and actual reported string are both recorded. Z3 is available as an alternate
solver but the acceptance suites use Yices. No floating tool download occurs.

## Actual frontend and property preservation

The plugin imports `hw/rtl/pe.vhd` or `hw/rtl/cgra_ctrl.vhd` and the shared
package. `pe_ports.vhd` only adapts record ports; it contains no ALU. The
SystemVerilog PE harness specifies independent unsigned low-half arithmetic
against the production signed VHDL ALU. Controller PSL binds to the production
architecture and observes its internal registers, with separate capture/age
monitors. There is no replacement controller implementation.

Two compatibility details were found by execution:

- GHDL synthesis generic overrides must precede source filenames in this
  invocation syntax. Putting them after sources silently uses default values.
  The runner checks flattened port widths and inserts a constant assertion for
  **every** selected generic into the existing PSL unit.
- This pinned frontend uses the last `vunit` bound to an architecture; it does
  not combine two units as initially expected. Generated instance assertions
  therefore belong to the same unit as the behavioral assertions. The original
  property file and effective generated file are both saved and hashed.

`--task ID` selects an explicitly labelled subset for investigation; it never
claims that the whole named suite ran. Array composition automatically adds
the required universal PE proof, even in the cover suite. Prepared source
copies and the exact runner are retained in each new run directory.

After preparation, the runner checks expected assertion/assumption/cover cell
counts and counts again in the **SMT model actually given to the solver**.
User assumptions are distinguished from Yosys initialization constraints.
`--no-assert-cover` suppresses GHDL's automatic assertion-activation covers;
explicit covers remain. Neither `--no-formal` nor `--assert-assumes` is used.
Constants and identical trivially true parameter-specific lemmas may merge;
the expected counts account for the enumerated instance. A mismatch fails.

For the full PE arithmetic proof, flattening, width reduction, techmap and
common-expression optimization make equivalent arithmetic visible to the
solver. They preserve the full 16-bit operands and opcode/selector domains.
The first hierarchical formulation timed out at 120 s; that is not a proof.

## Current suites and limits

All tasks use one rising-edge clock and the RTL's production initial values.
Inputs are symbolic two-state bit vectors. A sampled synchronous reset can
occur subsequently at any cycle. Hardware power-on behavior outside that
initialization/reset model is not a claim of these proofs.

| Task / requirement | Inputs and assumptions | Acceptance |
|---|---|---|
| F-PE-SMOKE: reset priority, hold, NOP, CONST | Initial reset only; later reset/step/config/neighbors unrestricted | BMC base and temporal induction; depth 8 |
| F-PE-ALU-MUX: all 16 opcodes, all 8 selectors per mux | Same; all 16 operand bits, reserved selector 7 is zero | BMC base and temporal induction; depth 4 |
| F-PE-NEGATIVE | Same; deliberately false post-reset assertion | Must FAIL with a reachable trace |
| F-CTRL-1x1-DIV1/2/3/4 | All RX bytes/valid, TX busy, reset and PE inputs unrestricted; watchdog 3 | BMC base and temporal induction; depth 16 |
| Full controller: 2x3, 3x2, 4x4, 1x16, 16x1 | Same; STEP_DIV=2, production watchdog 100000000 | Same proof criterion |
| F-ARRAY-1x1 | Initial reset, otherwise all inputs unrestricted; complete production RTL | BMC base and induction, depth 4 |
| F-ARRAY-2x3 (fast); 1x2, 2x1, 3x2, 4x4, 1x16, 16x1 (full) | Initial reset; imported production array wiring; proved PE transitions as component guarantees | BMC base and induction, depth 4, assume-guarantee composition |
| F-UART-TX/RX-C3 (fast), C868 (full) | All sampled digital inputs and resets unrestricted; production initial registers | BMC base and induction, depth 16 |
| F-BLOCK-DOT-DIV1/2/3/4 | `count <= 1024` (VHDL port subtype); other inputs unrestricted | Sequencer control; BMC base/induction depth 16 |
| F-BLOCK-MEMORY-DIV2 (fast); DIV1/3/4 (full) | Same count bound; two actual 1024×16 banks, arbitrary constant address, all 16 data bits | Accepted write retention, synchronous reads and ordered operand capture |
| F-BLOCK-RX-T3 (fast); T100000000 (full) | No input assumptions; actual 144-byte staging and all wire values | Length/order/CRC, acceptance/rejection and watchdog safety |
| F-BLOCK-CTRL-DIV2 (fast); DIV1/3/4 and T100000000 (full) | Initial synchronous reset only; subsequent RX, TX busy, reset and PE result unrestricted | Integrated bounds, validated effects, ownership, cache/replay and TX handshake |
| M-BLOCK-EARLY | Temporary sequencer copy adds a capture in S_READ | Must FAIL with trace |
| M-PE-ADD, M-CTRL-STEP, M-EXEC-BYTE, M-ARRAY-NORTH, M-UART-TX/RX | Temporary DUT copies with targeted mutations | Must FAIL with trace; original RTL unchanged |
| Cover suite | PE, four 1x1 controllers, 1x1/2x3 arrays, C3 UARTs; extra 2x3 EXEC witness | All explicit covers within 40 cycles (UART: 80; EXEC 2x3: 96); includes the universal PE obligation |

The PE contract includes modulo wraparound, low-half MUL/MAC, ABS(-32768),
signed MIN/MAX, arithmetic right shift and four-bit masked shift amounts.
Covers include each opcode, nonzero output, wraparound, ABS(-32768), signed
shift and self feedback. The single all-opcode assertion is universal over
the opcode, not a selection of example operand values.

Controller requirements have stable `F_CTRL_*` names in `ctrl_contract.psl`:
reset; legal indices; step context and progress; independent pending capture
count; RUN/EXEC completion with zero remaining captures; reset/first-capture
guard and minimum capture spacing; checksum-rejected EXEC without step/reset;
parser timeout; TX launch context and data stability during the handshake.
`F_CTRL_CFG_CHANGE_CONTEXT` and `F_CTRL_INPUT_CHANGE_CONTEXT` restrict actual
register changes to reset or their receive states. `F_CTRL_DATA_CAPTURE_GUARD`
proves at least STEP_DIV-1 intervening cycles between CFG/WR receive events and
a non-reset capture. Together with capture spacing/reset guard this supports
the multicycle data constraint; CE/reset timing remains single-cycle. The
`C_CTRL_DATA_CAPTURE` cover reaches a capture after actual changes to both CFG
and input registers, preventing unchanged-zero data from satisfying that cover.
DIV=1 permits consecutive captures. Reachable-state lemmas about return states,
indices, phases and pending count are **asserted and proved**, never assumed.
The capture budget covers the whole 8-bit request domain, including 0 and 255;
the current 40-cycle cover does not demonstrate completion of a 255-step run.

No unconditional liveness claim is made: TX busy may stay high or low forever.
The watchdog range/tick/clear invariants and next-state timeout assertion
prove the counter progress and return to idle once the timeout condition holds. The production timeout counter is present in full tasks;
reaching 100000000 timeout cycles is not attempted by the short cover suite.
The enumerated geometric instances are not a parameterized theorem.

## Array composition

`array_ports.vhd` only flattens ports, with word i in the low-first row-major
slice. Each assertion compares a PE output against its previous-cycle
transition, with neighbors selected independently from that register vector.
Every opcode, both selectors, config word and 16-bit edge input remain symbolic.
This checks all four directions, border injection/zeros, simultaneous capture,
hold/reset and isolation from other PEs' configuration at the same capture.

A monolithic 2x3 proof timed out at 90 seconds. The larger array tasks therefore
replace only the imported `pe_Brtl` cells by `pe_assume.sv` component contracts;
they **do not constitute a monolithic solver proof of the complete netlist**.
`F-PE-ALU-MUX` proves that very transition on production `pe.vhd` first. The
runner refuses composition if the obligation fails or RTL/spec hashes differ,
and checks the full PE interface and exact instance count before substitution.
An N-PE task has N transition assumptions plus one initial-reset assumption.
Abstract initial PE outputs are unconstrained (an overapproximation); the
first reset makes their next outputs zero. No operand/opcode narrowing occurs.

The north-to-self mutation uses distinct CONST values followed by PASS-north
as a **negative-only stimulus**, not as a restriction on the universal proofs.
Its BMC depth is 6: depth 4 did not reach the registered assertion after reset,
seeding and routing, and was correctly rejected as a failed negative control.
The reached failure is also covered by the existing GHDL `tb_array_geometry`
distinct-seed/all-neighbor regression, so no duplicate testbench was added.

## UART digital contract

TX properties cover reset, captured-data stability, idle/start/eight data bits/
stop, exact bit periods, LSB-first order and return to idle. Start during busy
is unrestricted in the safety proof; external callers still obey the RTL port
contract (start/data while idle). RX properties check the two sampled delays,
start confirmation/glitch rejection, bit periods/index, one-cycle valid,
stop rejection, break suppression and recovery. An independent monitor stores
samples by increasing bit index, rather than duplicating the DUT shift register.
Its partial-word invariants and the START counter bound are asserted and proved.
`prev` comparisons are guarded until the first sample exists.

Both CLKS_PER_BIT=3 and the production value 868 have universal induction
proofs. Covers use only the reduced instance: all completion/error/reset paths
must be reached within 80 cycles. Separate GHDL `tb_uart_async` drives the line
without the DUT transmitter at divisors 16 and 868, with 12 phase/bit-period
combinations (nominal and ±2%), adjacent frames, glitch, invalid stop, break
and reset. Neither proof nor digital simulation certifies metastability.

## EXEC response and conditional completion

`exec_contract.psl` extends the same controller vunit with independent request
checksum, response prefix count, queued-byte content/checksum/status and exact
length monitors. All PE bits, mask bits and RX bytes remain symbolic. No
assumptions are added to the controller proof tasks. Natural-valued progress
ranks are proved for request reception, RUN, EXEC response and the production
UART TX. Under explicit byte-delivery and transmitter-service obligations,
these well-founded ranks establish conditional completion. This is a ranking
certificate plus its mathematical argument, not a separate LTL-liveness run.

See [completion.md](completion.md) for the exact ranks, sampling points,
component obligations and reset/fairness limits. An external producer may
change PE inputs; the controller proof checks each byte's mux sample. The
separately proved array hold contract gives a stable response in the integrated
RTL. The extra 2x3 cover uses six explicit witness-only assumptions; universal
proofs do not use them. A low/high-byte mutation must yield a counterexample.

The dedicated formal CI job runs the pinned dependency check and fast suite,
retaining models, logs and counterexamples on failure. Software, HDL/scripts,
documents and Nix packaging have separate jobs.
The Nix `checks.x86_64-linux.formal-fast` derivation executes the fast suite;
it has also been built successfully, independently of the development shell.
The `full` target expands the PE/array/controller/UART contracts, including
the complete EXEC response and ranking invariants. It does not certify
end-to-end host/line delivery or the experimental block extension.

## Artifacts and status

Each run lives in `hw/build/formal/SUITE-UTC-PID/`. Its manifest contains source,
property, adapter, runner and solver-model hashes; actual commands/versions;
parameters; assumptions; depth; durations; logs and trace paths. Source files
are copied before execution so later edits do not change the evidence.
Completed historical evidence is archived under `doc/verification/` separately.

- `PROVED`: base case and temporal induction both pass; not merely depth N.
- `BOUNDED_PASS`: finite BMC, or finite cover reachability (mode distinguishes).
- `FAIL`: a reachable assertion counterexample, including expected controls.
- `UNKNOWN`: e.g. induction does not close although the finite base case passes.
- `TIMEOUT`, `ERROR`: solver budget or tool/property-preservation error.
- `NOT_RUN`: use for an explicitly unexecuted requirement, never as a PASS.

Expected negative tasks retain result `FAIL` and separately set
`acceptance_passed`; a missing counterexample is an acceptance failure. Normal
mandatory tasks require PROVED, cover tasks require all covers reached. The
default per-task solver budget is 90 seconds; fast has eighteen tasks, so budget
30 minutes for CI including frontend overhead and dependency startup. Actual
durations belong to individual manifests, not timing guarantees.

## Primary documentation

- [GHDL synthesis and formal switches](https://ghdl.github.io/ghdl/using/Synthesis.html)
- [GHDL PSL subset and vunit](https://ghdl.github.io/ghdl/using/ImplementationOfVHDL.html)
- [GHDL Yosys plugin](https://github.com/ghdl/ghdl-yosys-plugin)
- [SymbiYosys](https://yosyshq.readthedocs.io/projects/sby/en/latest/)

The online GHDL pages described 7.0.0-dev when consulted. Compatibility claims
here come from the executed pinned 6.0.0 flow, including negative probes.

## Experimental block contracts

The properties observe the production VHDL; no C or handwritten RTL model
replaces the controller, receiver or memories. Standalone sequencer tasks
prove 24 assertion cells with one input assumption and three covers. The
assumption represents the VHDL `count` subtype 0..1024, whose synthesized port
also encodes 1025..2047. All 16 data bits remain symbolic. The capture budget,
nonempty fetch/read/wait phases, guard, ownership, reset and result capture are
checked together. The PE result input is symbolic; its arithmetic is proved
separately by the PE tasks.

### Memory contents

`block_memory.psl` adds seven assertions and three covers to the same vunit.
A GHDL `anyconst` attribute supplies a single arbitrary ten-bit address. The
runner verifies its width both before solving and in the actual SMT model,
and checks that both 1024×16 memories survive preparation. There is no reduced
RAM model. For either bank, the most recent accepted write to that address
must remain stored until the next accepted write. Writes during busy/reset
are not accepted. Monitors also check synchronous read data and operands at
capture, including read-before-write behavior. Initial unwritten contents
remain unconstrained; ghost validity persists across reset because RAM does.
Covers include nonzero data, overwrite followed by reset, and index greater
than zero. `M-BLOCK-READ` deliberately swaps the read bank in a temporary copy.

### Frame receiver

`block_rx_contract.psl` has 22 assertion cells and no assumptions. It checks
all 144 byte positions and the full wire domain: ordered append without prefix
overwrite, canonical escapes, overflow discard, valid version/length, CRC
acceptance and rejection, one-cycle publication, reset/ready isolation and
watchdog progression. A separate CRC recurrence uses explicit polynomial taps
12/5/0 rather than calling the DUT's shift/XOR function. Timeout 3 is used in
fast and covers; full also proves the production 100000000-clock instance.
`M-BLOCK-CRC` bypasses the request CRC comparison and must produce a trace.

### Integrated controller and deduplication

`block_ctrl_contract.psl` includes the actual receiver and engine. It starts
with synchronous reset, matching the initialized reset synchronizer in
`cgra_top`; subsequent reset and all external input values are unrestricted.
There are 86 surviving assertion cells and one assumption. The runner removes
only the standalone engine's count *assumption* from the copied child vunit:
`F_BCTRL_COUNT` now proves the bound on its real connected input, alongside the
child's control assertions. The original/effective properties are both hashed.
No component is abstracted and no assertion becomes an assumption.

Checked properties include buffer/response indices and capacities, commit
bounds, exact upload word/address order and final committed length, lack of
concurrent writes during DOT, validated bytes held during
processing, effects authorized by a validated frame, resident length bounds,
TX launch/stability and mode isolation. A separate arbitrary eight-bit address
quantifies over every cached request/response byte. Prefix-match invariants,
cache stability, next sequence with no wrap, exact replay identity and response,
no duplicate reset/write/start/capture, and execution counter updates are proved.
Only the immediately preceding request in the active volatile session belongs
to this deduplication scope. No persistence across reset, collision-free CRC,
unique nonce, implicit retry, or unconditional liveness guarantee is implied.

For controller tasks, top output flags are removed before ordinary optimization;
the wires, all inputs and properties remain. This allows removal of RAM data
that cannot affect these control properties. The separate RAM tasks keep both
full memories. Property counts are checked after optimization and in SMT.

### Reachability, budgets and limits

The controller cover uses `block_ctrl_witness.vhd`, an executable driver around
unchanged production RTL: initial reset, preflight, HELLO, then a choice of
upload, empty DOT, exact replay or conflict. An arbitrary two-bit selector
ranges over all four transcripts. Prompt TX service and explicit wire bytes
are **stimulus for reachability only**, absent from universal proof tasks.
The driver replaces no DUT logic. CRC fixture constants come from Python
`binascii.crc_hqx`; ordinary transcript tests separately exercise C/RTL framing.

This cover unrolls the entire prefix from reset, but searches for the five
covers in cycles 160..199 (depth 200). `skip=160` suppresses earlier cover queries;
it adds no assumptions about skipped assertions or states. Child covers run
separately at depth 40. A trivially true generic check merges during cover
optimization (85 assertion cells remain); universal controller proofs retain
and prove all 86. The driver selector must survive into SMT. Budget: 90 seconds
per ordinary task; 240 seconds for this controller cover. `--timeout` explicitly
overrides these values. Earlier timeouts, missing invariants and unsupported
frontend constructs remain in their own evidence directories.

These are selected safety properties on enumerated instances, not a proof of
all block API software, every formatted reply, physical timing, metastability
or whole-system liveness. `tb_block_faults` additionally tests reset during
upload/DOT and ignored concurrent requests during execution/readback. Both
new mutations are replayed against actual GHDL testbenches. Physical results
retain the unchanged production RTL hashes; formal-only edits need no new STA.
