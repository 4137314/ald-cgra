# Resident blocks: integration and final gates

Source baseline: `78100d8b683f725e2a9cce6225161c09c642f93a`, initially clean.
Runs below exercised the subsequent dirty tree, with exact source hashes in
individual manifests. They are not attributed retroactively to a later commit.
Commands, actual tools, exit statuses and build flags are in those manifests,
compiler logs, physical snapshots and Nix derivations. No board was programmed.

## Executed gates

| Evidence | Command / result |
|---|---|
| [Release](../2026-10-07/block-release/) | `make -j4 test`: 3519 named checks PASS |
| [Sanitizers](block-asan/) | `make -j4 PROFILE=asan test`: 3519 checks PASS, ASan/UBSan/LSan active |
| [GCC 13 / Clang 18](block-compiler/) | Release and sanitizer suites PASS for both; 3514 checks per run, five high-FD cases explicitly skipped due to container limits; GCC analyzer PASS |
| [Tooling](block-tooling/) | GCC analyzers, 12 install / 211 compdb / 27 incremental-build checks PASS |
| [Scripts](block-scripts-final/) | 85 Tcl / 37 artifact checks PASS; mock Tcl checks are distinct from actual Vivado below |
| [GHDL](block-ghdl-final/) | Full `make -C hw -f hw.mk sim` PASS, including 886 block events for DIV1/2/3/4 and actual top-level UART DIV2 |
| [Nix](block-nix/) | Actual `checks.x86_64-linux.software` and `formal-fast` builds PASS, including no-readline and analyzer gates; immutable store input/derivation retained |
| [Full formal](block-runs/formal/full-20261008T091350Z-2776760/) | 27 PROVED and eight expected FAIL; acceptance PASS |
| [Cover](block-runs/formal/cover-20261008T092009Z-2809079/) | One PE obligation PROVED, 15 BOUNDED_PASS, depth 40/80/96 by task; acceptance PASS |

Fast in the Nix check has 14 PROVED plus one expected FAIL. The block sequencer
has 23 assertion cells and one assumption (count≤1024, its VHDL port subtype),
on each DIV1–4 instance. All data remain full 16-bit. Result capture treats the
PE result as symbolic; PE arithmetic is proved separately. RAM conservation,
caller length validation, framing/CRC and duplicate cache are **not yet formal
proofs**. Covers are reachability, not universal claims. Counters are checks,
not code coverage percentages. See the [requirements matrix](../../verification-matrix.md).

## Measured traffic and physical implementation

[Five separate API measurements](../2026-10-07/block-api-traffic/) execute dot256
8 times, input `a=i%7-3`, `b=i%5-2`, nonce `01020304`, scalar int64 oracle:

| Geometry | Legacy bytes / transactions | Resident bytes / transactions |
|---|---:|---:|
| 1×16, 16×1 | 86840 / 2072 | 1856 / 26 |
| 2×8, 8×2 | 58168 / 2072 | 1856 / 26 |
| 4×4 | 49976 / 2072 | 1856 / 26 |

Legacy excludes initial ID and includes CFG per call; resident includes
preflight, HELLO, uploads, DOT and RESULT. Earlier private-model measurements
omitted the six-byte preflight. These are traffic counts, not FPGA timing or
CPU speedup. Only PE0 computes; 2048 captures/products and 2040 useful additions.

Actual Vivado 2026.1 / Nexys A7 / 4×4 / STEP_DIV=2 / 10 ns:

- [Legacy synth](block-runs/physical/synth-20261008T091642Z-dbfbddce/) and
  [enabled synth](block-runs/physical/synth-20261008T092712Z-75b0e188/): identical
  source/tool hashes and `flatten_hierarchy none`; 5017→8459 LUT,
  1034→4348 FF, 16→16 DSP, 0→1 BRAM tile (two RAMB18).
- [Enabled routed](block-runs/physical/sta-20261008T091011Z-d4eb1e5f/):
  **WNS +0.427 ns, WHS +0.060 ns**, 8164 LUT / 4346 FF / 16 DSP / 1 BRAM tile.
  The rebuilt strategy differs from the synth pair. Setup/hold, DRC, CDC,
  synchronizers, all 833 launch registers and 256 data-D endpoints passed.
- Earlier real STA failures, −3.580 ns and −2.867 ns, are retained. They led to
  explicit operand and ownership registers with proved guard intervals.
  CE/reset and RAM-to-operand paths remain single-cycle. The initial boolean
  generic elaboration failure is also preserved, not counted as a synthesis.

## Archive rules and limits

[Archive index](block-runs/index.json) links immutable original run manifests.
Per-run `archive-index.json` records every original file's size/hash, whether it
is included, and deterministic gzip paths for large JSON traces. 2977 included
files were decoded and checked against their original SHA-256 values. Generated
RTLIL/SMT2 and physical DCP/bitstreams are omitted with hashes retained; this
archive is not a programming artifact. Actual source copies and commands allow
reproduction. Nix output paths are separate immutable evidence.

[Development logs](block-development-logs/) preserve intermediate failures and
exploratory output without inventing exact source snapshots. They are explicitly
not acceptance evidence. Earlier source/proof-count errors remain ERROR/FAIL in
their own manifests. The final stricter Make `BLOCK_ENABLE` validation was
checked by `block-scripts-final`; it postdates the Nix source snapshot and does
not change the software or formal RTL exercised by Nix.

No physical board test, analog metastability certification, whole-stack formal
claim, or unconditional deadlock guarantee follows from these results.
