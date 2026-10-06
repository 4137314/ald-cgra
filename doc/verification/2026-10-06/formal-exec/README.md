# EXEC response and progress certificates — actual formal executions

Pinned GHDL 6.0.0 / plugin 1b97dc7 / Yosys 0.67 / SBY 0.61 / Yices 2.7.0.
Production RTL is unchanged. The controller monitor is extended inside the
existing vunit, with independent request checksum, response byte/rank/length,
queue, status and progress invariants. The TX monitor gains a natural-valued
frame progress rank. Contracts: `hw/formal/README.md` and `completion.md`.

Final accepted standalone runs:

| Suite | Run | Actual outcome |
|---|---|---|
| fast | `fast-20261006T135826Z-2072021` | 10 PROVED, one expected FAIL with witness |
| full | `full-20261006T140029Z-2077694` | 23 PROVED, seven expected FAIL with witnesses |
| cover | `cover-20261006T140029Z-2077690` | one required PE PROVED; eleven BOUNDED_PASS reachability tasks |

PE/array/controller/UART remain the production designs. Controller safety and
rank proofs have **zero assumptions**, all 16-bit PE inputs and RX bytes/masks
symbolic. DIV1/2/3/4 at 1x1; five larger geometries at DIV2. Temporal induction
closes, so positive proof results are not just the 16-cycle base case.
Conditional completion follows the proved well-founded ranks under the
explicit service/delivery obligations in the contract. No separate LTL engine
run, unconditional deadlock freedom or physical UART guarantee is claimed.

The ordinary covers keep arbitrary inputs. An extra 2x3 EXEC cover uses six
clearly marked witness-only assumptions (prompt valid EXEC, one step, prompt
TX, no reset). It reaches empty, full/nonempty execution, sparse and NACK
responses within depth 96. These restrictions are never in the universal
proof tasks. Other cover depths: 40, or 80 for UART. Cover is reachability only.

`M-EXEC-BYTE` supplies the new seventh negative control: a temporary low/high
byte mutation gives a reachable assertion failure. Existing GHDL protocol
replay also rejects that mutation (`../exec-ghdl/`); unmutated RTL passes all
504 transcript transactions and watchdog recovery.

All exploratory runs are retained in `archive/` with exact inputs/results:

- first inductive formulations returned UNKNOWN (bounded base case passed);
  reachable-state invariants were added and proved, not assumed;
- an intermediate property-count mismatch remained ERROR despite solver PASS:
  the watchdog range property survived and had been omitted from the expected
  count. Counts were corrected; no property was deleted;
- unconstrained extra 2x3 covers reached only part of the target set before a
  real 90-second timeout. The raw solver status and log remain, even when the
  simultaneous count mismatch made the outer result ERROR. The final extra
  cover has an explicit witness model, without weakening any proof.

`archive.py` was executed to copy inputs, merged properties, commands, logs,
results and VCD/Yosys witnesses. `index.json` hashes the archive. Generated
RTLIL/SMT/netlist binaries/text remain in build/; their hashes and reproduction
commands are in each result. Historical logs have not been rewritten.
