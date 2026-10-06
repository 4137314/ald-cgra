# EXEC response and conditional completion contract

This contract applies to the production `cgra_ctrl` instances enumerated in
[README.md](README.md). `exec_contract.psl` is appended inside the existing
controller vunit; GHDL's last-vunit behavior makes a second independently bound
vunit unsuitable. The original fragments, merged effective unit and solver
property counts are all retained. No controller assertion becomes an assumption.

## Interface and observation points

One rising-edge clock; production initial register values; subsequent reset,
RX bytes/valid, TX busy and every PE input bit are unconstrained in the safety
and ranking tasks. A synchronous external reset aborts a transaction. Guarantees
of a complete response therefore require no intervening external reset.

The receive monitor sums accepted header/input bytes as integers modulo 256,
independently of the RTL's unsigned checksum register. It checks the header
field assignments and captures the verdict against that sum. Reserved flags
and mask bits outside the instantiated PE count remain symbolic and retain
the legacy ignored-bit behavior. CFG/WR, including EXEC's edge inputs, can
change before rejection: none of these properties requires rollback.

The response monitor counts observed `tx_start` launches. It proves:

- exactly `2 * popcount(mask within this geometry) + 2` bytes before returning
  to idle: selected low/high tap bytes, their sum modulo 256, then ACK/NACK;
- selected tap indices ascend in row-major order, using an independent prefix
  population count; skipped taps add no response bytes;
- a queued byte's contents stay correct and it launches once; every launch
  consumes a queued byte, and no launch exceeds the response length;
- request checksum rejection produces no datapath reset or capture throughout
  the response, not merely at the rejection edge;
- indices, byte phases and the receive checksum agree with independently
  observed progress. All strengthening invariants are assertions to be proved.

`F_EXEC_MUX` checks the registered read mux against independently saved PE
inputs and index. `F_EXEC_SETTLED` proves the selected index has settled before
use. Thus low/high bytes refer to the corresponding input sample **one clock
before byte preparation**; this proof allows PE inputs to vary every cycle.
It does not assume an arbitrary external producer holds an entire response
snapshot. In the real array the separately proved PE hold transition applies
to every PE, and the controller emits no further capture/reset while replying.
The initial read-settle cycle follows the last capture. Those component
contracts give a stable post-execution value through both bytes and all taps.

## Completion by a well-founded ranking certificate

The solver proves safety statements about finite, nonnegative integer ranks,
using base case and temporal induction. The conditional eventual-completion
argument below follows from those certificates. This is **not** a separate
LTL-liveness engine run and is **not** an unconditional deadlock claim.

Let `P = rows * cols`, `D = STEP_DIV`, `w = word_idx`, `b = byte_idx`.
`F_EXEC_REQUEST_*` proves that the request rank is positive while receiving,
never increases, and strictly decreases on every delivered payload byte:

| Receive state | Remaining rank |
|---|---|
| Header | `5 + 2*(rows+cols) - hdr_idx` |
| Edge inputs | `2*(rows+cols-w) + 1 - b` |
| Request checksum | `1` |
| Aborted/finished receive | `0` |

For RUN, including plain RUN commands, the rank is
`run_cnt*D + run_wait + D - run_phase`; it is positive in S_RUN and strictly
decreases each reset-free clock until S_RUN ends. This covers the full
8-bit count, including 0 and 255. It does not enumerate/unroll 255 captures.

For an active EXEC response, `F_EXEC_RANK_*` proves nonincrease at every
reset-free clock, positivity outside idle, and strict decrease whenever the
current state can progress. The rank is:

| State | Rank |
|---|---|
| RUN | `8*P + 10 + run_cnt*D + run_wait + D - 1 - run_phase` |
| Read tap | `8*(P-w) + 8 - 3*b + rd_settle` |
| Prepare checksum / status | `8` / `5` |
| SEND returning to read / checksum / status / idle | `8*(P-w)+10-3*b` / `10` / `7` / `2` |
| SEND_WAIT with the same return destinations | preceding SEND rank minus one |
| Idle | `0` |

Progress occurs every clock in compute/prepare states, when TX busy is low in
SEND, and when TX busy is high in SEND_WAIT. Other clocks may stutter; they
cannot increase the rank. A finite natural number cannot strictly decrease
infinitely. Therefore, **assuming service eventually occurs whenever either
TX wait persists**, an accepted EXEC checksum leads to idle after exactly the
proved response length. This includes NACK and an empty mask. The argument
allows arbitrary finite TX stalls and puts no numerical deadline on them.

For the request prefix, the environment must deliver its finite byte sequence
before each watchdog expiry. If delivery stops, the watchdog instead aborts:
`F_CTRL_WD_RANGE/TICK/CLEAR` proves counter range/increment/reset, and
`F_CTRL_WATCHDOG` proves return to idle at expiry. A clockless C model does not
prove that temporal behavior. Plain RUN completion uses its strictly decreasing
compute rank, then the same finite ACK handshake under TX service.

## Discharging the production transmitter obligation

No abstract transmitter assumption is imported into the controller safety or
ranking solver tasks. The conditional theorem states its service premise
explicitly. For the actual connected transmitter, the separately proved
`uart_tx` contract supplies the relevant obligations:

- idle and no start remains idle and clears busy;
- start in idle captures the word and raises busy on the next sampled state;
- a nonidle TX state has busy high;
- `F_TX_RANK_PROGRESS` strictly decreases `10*C-clk_cnt` in START,
  `(9-bit_idx)*C-clk_cnt` in DATA, and `C-clk_cnt` in STOP until idle.

Here `C = CLKS_PER_BIT`, proved separately at 3 and the production value 868.
Together with idle behavior this bounds completion of an accepted frame, even
if new start inputs are presented during a busy frame. The integration has one
controller as the only start producer; its start pulse follows observing busy
low. These port connections and the proved idle behavior permit acceptance;
no other producer can steal the idle cycle. A reset aborts both components.
The argument concerns the digital handshake, not whether an external UART
receiver sees the analog line or a host application reads a response.

## Reachability, negative control and remaining scope

The ordinary 1x1 covers keep arbitrary inputs. The extra 2x3 EXEC cover has a
**reachability-only** stimulus (`exec_cover.psl`): no reset, prompt EXEC bytes,
one requested step, and a prompt abstract TX handshake. Mask, payload and
checksum remain symbolic. It searches only empty/full/sparse/NACK responses;
full-mask completion also requires an observed capture. These six assumptions
are never applied to a universal proof. They describe witness construction,
not a production UART timing model or additional universal guarantees.

`M-EXEC-BYTE` swaps the low byte for the high byte in a temporary copy of the
production controller. Acceptance requires an actual assertion counterexample.
The same mutation is detected by the existing GHDL protocol replay; there is
no replacement RTL or manually rewritten controller in the proof.

The contracts do not prove every possible host framing corruption recovers,
physical timing/metastability, software correctness or the future block
extension. The geometries are enumerated instances, not a generic theorem.
