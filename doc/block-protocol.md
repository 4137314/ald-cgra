# Experimental resident-block protocol v1 — implementation contract

**Status: private C codec and functional model executed; controller, public
API/CLI, memory inference and end-to-end integration remain B01–B03 work.**
Legacy ID continues to advertise v2/v3. No current device capability or FPGA
result is implied by this document or the private model's HELLO response.

## Selection and framing

The extension will be selected explicitly. The planned preflight is a single
legacy-idle opcode `0x08`: an enabled implementation replies
`CB 01 40 00 04` (marker, block revision, 64 words/chunk, 1024-word capacity
little-endian). An old controller NACKs that one byte. A host must validate
preflight before sending framed payload, so probing old RTL cannot accidentally
interpret payload bytes as legacy commands. This preflight is **not yet wired**.
Mesh addressing remains at most 16 PEs; block capacity is a separate limit.

A frame has a leading `7E`, escaped body and trailing `7E`. Inside the body,
`7E` becomes `7D 5E`, and `7D` becomes `7D 5D`; other escape pairs are invalid.
Empty flag runs are fill. Every frame has its own leading/trailing delimiter;
do not share one delimiter between adjacent frames. Noise outside a frame is
ignored by this decoder. The future legacy/block dispatcher must isolate the
framed receive interval from legacy command decoding.

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
be trusted. Decoder reset discards any unfinished frame. A timed watchdog and
UART backpressure behavior still require RTL, not this clockless C model.

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
then return SESSION, never execute. A new session must not reuse a nonce while
old traffic could remain. A random 32-bit nonce reduces accidental reuse but
cannot mathematically eliminate it. The scope is a serialized connection and
one cached request; it is not durable exactly-once execution across resets,
unbounded delayed traffic or multiple concurrent hosts. CRC is error detection,
not authentication: multi-byte corruption can collide. Neither CRC nor sequence
numbers guarantee rejection of every possible corruption.

Lost responses may be recovered only by explicitly retransmitting the exact
same framed request in this deduplication scope. The legacy transport rule
(no implicit retry after uncertainty) remains unchanged. Public recovery and
whole-transaction deadline integration still need implementation/tests.

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

The planned RTL sequencer uses the existing production MAC PE with resident
operands and a guard compatible with STEP_DIV. A complete integration must
specify legacy configuration/register ownership, prove reset/capture guards,
exercise backpressure/partial frames, and verify local-memory inference using
real synthesis. At this checkpoint there is **no BRAM-use or FPGA-performance
claim**. The C model is a functional oracle, not a timing model.

## Executed checks and next integration gate

`make -C lib -f lib.mk test` includes `test_blocks`: CRC check vectors, every
payload length, escaping, insufficient capacity without output writes,
fixture bit flips/byte deletion/insertion, oversize/discard/resynchronization,
framed request/response round trips, resident dot against an independent signed
scalar oracle, exact duplicate/conflict behavior, sequence exhaustion and reset.
Wire counts come from actual encoded request/response buffers. These tests do
not by themselves close B01–B03 or advertise a new public capability.
