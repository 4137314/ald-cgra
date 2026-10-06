# Synchronizers, multicycle constraints and formal guards

The 30 September runs use real Vivado 2026.1 (SW build 6511674), Nexys A7
xc7a100tcsg324-1, 4x4, STEP_DIV=2, 10 ns, default implementation directives.
The source snapshots preserve exactly the inputs used; no old log was changed
when the integrated flow was developed. Archives were completed on 5 October.

## Physical comparison

The separate baseline in `../vivado-baseline/unrestricted/` has 5016 LUT,
1034 FF and 16 DSP after synthesis with hierarchy none. It reports two CDC-2
warnings for missing ASYNC_REG. Its cell-endpoint multicycle covers 512
endpoints instead of the 256 intended PE D pins. Counts alone do not establish
which control paths were relaxed; the corrected flow inspects them directly.

`routed-first/` is a genuine failed attempt: Vivado rejected Q as a multicycle
startpoint. No success is inferred from the earlier Tcl mock checks.

`routed-second/` passes with C launch pins and D capture endpoints:

- setup WNS **+1.862 ns**, hold WHS **+0.098 ns**;
- implemented **4971 LUT, 1034 FF, 16 DSP, 0 BRAM tiles**;
- ASYNC_REG present on four stages, first stages drive only second-stage D;
- each RX/reset pair placed within one slice; report includes LOC/BEL/fanout;
- two CDC-3 informational entries; PE CE/reset paths each require one period;
- exception coverage, timing, DRC and source/checkpoint hashes retained.

These are a routed inspection and a separate synthesis baseline, not before/
after resource results with identical synthesis hierarchy options. No board
was programmed. False paths, placement and STA do not certify metastability,
physical serial behavior or the complete accelerator. See the production
physical-flow contract for the integrated gate added afterward.

## Formal

`formal-archive/full-20260930T140233Z-2/manifest.json`: 23 PROVED tasks and six
expected FAIL controls with traces. The extended controller monitor checks
CFG/input change context and the delay from receive writes to capture.
Inputs remain symbolic, including arbitrary bytes, reset and handshake timing.

`formal-archive/cover-20260930T140235Z-2/manifest.json`: universal PE obligation
PROVED and ten BOUNDED_PASS cover tasks. Each controller has a new cover for
capture after real CFG and input changes. Covers demonstrate reachability,
not universal correctness. Instance/assumption details are in hw/formal/README.

`fast-20260930T140027Z-2` preserves a frontend count ERROR for DIV1: the retained
assertion count was 30, not the anticipated 29. The expected count was corrected;
no assertion was removed or converted to an assumption. Other divisors proved.
`full-20260930T135350Z-2` separately proves RX at 3/868 with ASYNC_REG and detects
the bad-stop mutation. The archive index hashes original sources, effective
properties, runner, logs and Yosys witness traces. Large generated RTLIL/SMT
models are omitted, with their hashes retained in the original task results.

EXEC response ordering/checksum and conditional liveness remain F03 work.
