# Experimental resident-block protocol v1 — implementation contract

**Status: experimental RTL–model–public API–CLI path implemented and exercised.**
`G_BLOCK_ENABLE=false` remains the default. The enabled 4×4/DIV2 instance has
executed Vivado synthesis and routed timing reports with 1 BRAM tile (two
RAMB18), WNS +0.427 ns and WHS +0.060 ns at 10 ns; no board test has been run. Formal coverage includes selected sequencer, RAM, receiver and controller safety
properties. It does not prove the complete software/protocol stack or physical operation.

## Selection and framing

The extension is selected explicitly. The preflight is a single
legacy-idle opcode `0x08`: an enabled implementation replies
`CB 01 40 00 04` (marker, block revision, 64 words/chunk, 1024-word capacity
little-endian). An old controller NACKs that one byte. A host must validate
preflight before sending framed payload, so probing old RTL cannot accidentally
interpret payload bytes as legacy commands. Successful preflight **latches framed mode until external device reset**.
Closing/reopening a serial connection does not reset the device; restarting
`cgra emulate --blocks` does reset that model instance. Preflight must run once
per device reset; subsequent sessions use a new HELLO within the same handle.
Legacy ID and commands remain usable before preflight, including in the enabled
bitstream. In framed mode every received byte stays away from the legacy parser,
even after malformed input or watchdog expiry. There is deliberately no unframed
escape-to-legacy command which corrupt payload could accidentally invoke.
Mesh addressing remains at most 16 PEs; block capacity is a separate limit.

A frame has a leading `7E`, escaped body and trailing `7E`. Inside the body,
`7E` becomes `7D 5E`, and `7D` becomes `7D 5D`; other escape pairs are invalid.
Empty flag runs are fill. Every frame has its own leading/trailing delimiter;
do not share one delimiter between adjacent frames. Noise outside a frame is
ignored by the decoder. The dispatcher isolates the entire framed mode from
legacy decoding, including intervals between frames.

Escaping was chosen for a small streaming decoder with explicit boundaries.
It adds at most one byte per raw byte and two delimiters. COBS would bound its
extra bytes more tightly for payloads dominated by delimiter/escape values,
but needs a different length-code decoder. Actual encoded lengths, including
escapes, are measured; there is no constant-overhead traffic claim.

| Unescaped offset | Field |
|---|---|
| 0 | Block revision, `01` |
| 1 | Opcode; response echoes opcode with bit 7 set |
| 2–5 | Session nonce, uint32 little-endian |
| 6–7 | Sequence, uint16 little-endian |
| 8–9 | Payload length in bytes, uint16 little-endian |
| 10… | Payload, 0–132 bytes |
| Last two | CRC, little-endian |

CRC definition: width 16, polynomial `0x1021`, initial state `0xFFFF`, process
bytes MSB first, no bit reflection, final XOR zero. It covers **all ten header
bytes and the payload**, excluding delimiters/escaping and the CRC itself.
Check vector: ASCII `123456789` → `29B1`. Maximum unescaped frame is 144 bytes;
maximum encoded frame is 290 bytes. Version, length, canonical escaping and
CRC must all validate before publishing a request. Malformed/overlong frames
are discarded without a response or committed effects; their identity cannot
be trusted. Decoder reset discards any unfinished frame. The RTL receive watchdog discards unfinished input after G_TIMEOUT_CYCLES idle
clocks. It does not leave framed mode. Receive bytes during processing or reply
are ignored: the host must obey the single-outstanding-request contract and
wait for the complete response. UART TX waits for acceptance of each byte;
without transmitter progress no unconditional completion is promised.
The C model and PTY service remain clockless and do not implement that watchdog.

## Session and deduplication

Only one request may be outstanding. HELLO has sequence 0 and a nonzero new
nonce. It invalidates resident vector lengths/result and resets execution
count. Memory does not have to be physically cleared because invalid data is
inaccessible. Successful HELLO starts the expected sequence at 1.

For the current session:

1. The immediately preceding sequence with **identical opcode, nonce, sequence,
   length and every payload byte** replays the complete cached response.
   There is no repeated upload, reset or dot execution. CRC equality alone
   is never a duplicate identity test.
2. The preceding sequence with changed contents returns CONFLICT, retaining
   the original cache. Only the next sequence accepts a new command.
3. A valid in-sequence command consumes its ID and caches its response even
   when it returns a semantic argument/opcode error. Wrong session, wrong
   sequence and conflicting duplicates do not evict the cache.
4. Sequence 65535 exhausts the session. Wrapping to 0 is rejected. A HELLO
   with a new nonce explicitly starts another session and invalidates data.
   Reusing the active nonce for HELLO cannot reset the sequence.

An external reset loses the session and duplicate cache. Old non-HELLO requests
cannot execute. After selecting block mode again, old non-HELLO frames return SESSION. A new session must not reuse a nonce while
old traffic could remain. A random 32-bit nonce reduces accidental reuse but
cannot mathematically eliminate it. The scope is a serialized connection and
one cached request; it is not durable exactly-once execution across resets,
unbounded delayed traffic or multiple concurrent hosts. CRC is error detection,
not authentication: multi-byte corruption can collide. Neither CRC nor sequence
numbers guarantee rejection of every possible corruption.

Lost responses may be recovered only by explicitly retransmitting the exact
same framed request in this deduplication scope. The legacy transport rule
(no implicit retry after uncertainty) remains unchanged. The public API supports response and whole-frame transaction deadlines,
including writes; all response bytes share one deadline. It poisons a handle
after uncertainty and provides **no recovery/retry API**. Exact replay described
here is a wire-level facility for clients implementing the explicit contract.
A new serial handle alone is not recovery: the device may still be in framed
mode and require external reset. Diagnostics are owned by the existing handle.

## Commands and resident dot

All replies start with a one-byte status: OK=0, SESSION=1, SEQUENCE=2,
CONFLICT=3, OPCODE=4, ARGUMENT=5, NOT_READY=6. Successful reply extensions:

| Opcode | Request payload | Reply after status |
|---|---|---|
| 0 HELLO | empty, new nonce, sequence 0 | capacity uint16=1024; chunk uint8=64; capability uint16=1 (dot) |
| 1 UPLOAD | bank uint8 (0=A, 1=B), offset uint16, count uint8, count little-endian words | none |
| 2 DOT | element count uint16 | none |
| 3 RESULT | empty | result uint16; execution count uint32 |

UPLOAD count is 1–64. Offset zero replaces a bank's logical contents; later
chunks must append exactly at the current valid length. Gaps/overruns are
rejected before any memory change. A successful upload invalidates the result.
Each frame commits atomically after complete validation; a multiple-frame
upload may leave a completed prefix if a later frame fails. Semantic rejection
preserves data/result, while consuming the valid sequence as described above.

DOT requires the requested count to fit both valid banks. Its signed 16-bit
products and accumulation wrap modulo 2^16. Empty dot produces zero. It stores
a result and increments a uint32 execution counter modulo 2^32; the counter is
diagnostic, not the duplicate identity. RESULT is NOT_READY before a dot or
after an upload/HELLO. Repeating DOT with a new sequence reuses both banks;
repeating an identical sequence only replays the cached acknowledgement.

The RTL sequencer owns the existing array while DOT runs: PE0 is configured MAC
with west/north operands, all other PEs are NOP, the array is reset once, and
exactly `count` captures follow. Legacy CFG and input registers are preserved;
PE0 ends at the result and other PE registers at zero. New upload/HELLO do not
change PE registers; duplicate DOT does not reset/capture them again. Cached
plans are invalidated by preflight; legacy calls after selection return
`CGRA_ERR_UNSUPPORTED` without sending bytes.

Two 1024×16 banks use synchronous reads with no content reset. The controller
commits an UPLOAD over internal cycles after validating the whole frame; no
concurrent reader can observe its partial writes. External reset invalidates
session/lengths even if it interrupts a commit. Each DOT read has a guard before
capture; the current implementation takes `STEP_DIV + 3` clocks between words.
Only PE0 computes, trading array utilization for fewer host round trips. Synchronous RAM outputs first enter 32 explicit operand registers. Their
C-to-PE-D paths use STEP_DIV setup/STEP_DIV-1 hold, justified by the proved
operand-write context and data guard. A dedicated registered ownership signal
selects array configuration/data; its C-to-PE-D paths use the same constraint,
with ownership stability and guard proved separately. RAM-to-operand and CE/reset
paths remain single-cycle. The first design without the operand stage failed
routed setup (-3.580 ns); a second failed on state decoding into the array mux
(-2.867 ns). Both failure logs remain separate from the final passing run.

## Public use

```sh
cgra block-dot -d sim:blocks --a '1 -2 32767' --b '3 4 2' --repeat 3 --json
# {"count": 3, "result": [-7, -7, -7]}
cgra emulate --blocks --rows 2 --cols 3
# Use its printed PTY path for ONE block-dot invocation per server instance.
make -C hw -f hw.mk synth BLOCK_ENABLE=1
make -C hw -f hw.mk sta BLOCK_ENABLE=1
```

C API: `cgra_block_begin(dev, nonce)`, `cgra_block_upload(dev, bank, data, n)`,
`cgra_block_dot(dev, n, &result, &executions)`, `cgra_block_result(...)`.
All are additive to the existing ABI; the old PTY options layout also remains
unchanged, with `cgra_emulate_pty_with_flags(..., CGRA_PTY_BLOCKS, ...)` selecting
the extension. Upload accepts 1..1024 words and replaces a bank in chunks of
64; DOT permits zero elements. Each successful upload releases caller storage.
Outputs remain unchanged on errors; earlier successful upload chunks can remain
resident. Reserving sequence numbers for the entire upload or DOT+RESULT avoids
starting an operation that cannot fit before sequence exhaustion. CLI repeats
reuse both banks, return one result per DOT, and are bounded at 4096 results.
The CLI obtains a nonzero random 32-bit nonce; this is not guaranteed unique.

## Executed checks and remaining proof scope

- `make test`: codec/model plus public API across six geometries, full banks,
  signed extremes, scalar oracle, errors/output ownership, safe old-device
  probing. PTY tests cover fragmented writes, EINTR/EAGAIN, invalid CRC,
  one deadline across a slowly delivered response, poison/no implicit retries,
  and controlled service stop. CLI invokes the real PTY service too.
- `make -C hw -f hw.mk sim-blocks`: 886 transcript events for DIV1/2/3/4,
  then the actual top-level UART/dispatcher for DIV2. C generates frame bytes
  and replies; every executed DOT also uses an independent signed int64 oracle.
  Exact reset/capture counts distinguish replay from repeated computation.
  Corruptions include wire bit flips, inserted/lost bytes, truncated frames,
  reset, watchdog recovery, semantic failures and noise resembling legacy opcodes.
- `tb_block_faults` resets the controller during upload commit and DOT, then
  verifies new-session lengths/result state. Otherwise valid concurrent HELLO
  requests during execution or readback must not change the active session,
  result or number of captures. All four STEP_DIV instances are exercised.
- `F-BLOCK-DOT-DIV1/2/3/4`: 24 assertion cells, full data domain and the port
  subtype assumption `count <= 1024`. Index/count/cadence, nonempty active
  phases, operand/ownership guards, reset and result capture are checked.
- `F-BLOCK-MEMORY-*`: 31 assertions, both real 1024×16 banks. An arbitrary
  ten-bit address proves accepted write retention, reads and ordered capture
  across busy/reset/rewrite. Unwritten words remain unconstrained.
- `F-BLOCK-RX-*`: 22 assertions, no assumptions; CRC recurrence with independent
  bit taps, frame acceptance/rejection, canonical escaping, ordered staging,
  publication and watchdog. Full includes the production timeout.
- `F-BLOCK-CTRL-*`: 86 assertions with initial synchronous reset as the only
  assumption. Actual receiver/engine remain connected; the controller proves
  the child's count bound. An arbitrary byte index checks cache preservation,
  exact replay identity/response and no duplicate effects. Accepted effects,
  exact upload order/committed length, index/capacity limits, sequence exhaustion
  and TX handshake are also checked.
- Covers are finite reachability: component depth 40 and controller depth 200,
  with an explicit driver around unchanged RTL for the latter. Negative early
  capture, wrong-bank read and CRC-bypass mutations are required to fail.
  The last two also fail in GHDL. See [formal contracts](../hw/formal/README.md)
  for parameters, initial state, property preservation, budgets and limits;
  [new evidence](verification/2026-10-09/block-formal/) preserves failed attempts
  as well as final runs. No unconditional completion or whole-stack proof follows.

`python3 scripts/measure-blocks.py NEW_DIRECTORY` produces a separately measured
public-API point for each of five 16-PE shapes. Counts include preflight, HELLO,
upload, DOT and RESULT; legacy excludes initial ID and includes CFG per call.
This differs by six bytes/one transaction from the earlier private-model
comparison which omitted preflight. Counts are neither FPGA time nor CPU speedup.
Per-run physical/formal evidence and remaining B03 requirements are in `TASKS.md`.

## Physical cost and provenance

Actual Vivado 2026.1 runs on the same 4×4/DIV2 source snapshot and part,
with `flatten_hierarchy none`, give this synthesis comparison:

| Feature selection | LUT | FF | DSP | BRAM tiles |
|---|---:|---:|---:|---:|
| Legacy (`BLOCK_ENABLE=0`) | 5017 | 1034 | 16 | 0 |
| Blocks (`BLOCK_ENABLE=1`) | 8459 | 4348 | 16 | 1 |

The enabled routed run at 10 ns uses the separate rebuilt strategy:
8164 LUT, 4346 FF, 16 DSP, 1 BRAM tile, WNS +0.427 ns, WHS +0.060 ns.
This verifies inference and static timing for that instance, not hardware
operation or a higher oscillator frequency. The cost is significant: the first
extension prioritizes whole-frame validation and explicit deduplication; it is
not an area optimization. [Run manifests and reports](verification/2026-10-08/block-runs/)
retain each strategy, source hash, failure and successful gate independently.
No bitstream was programmed.
