# Report revision — 15 October 2026 (A03/A06 report part)

Base commit: see `git-state.txt` (HEAD plus the dirty doc/ paths at build time;
the working tree also contained unrelated staged checkpoint-13 changes).
No hardware, formal, Vivado or software suite was re-run for this revision:
the report only cites existing archived evidence, by directory:

| Claim in report | Source record |
|---|---|
| Bitstream gate 4×4, WNS +1.862 / WHS +0.098, 4971 LUT | `2026-10-05/physical/` |
| Five-shape sweep (Table VI, Fig. 9) | `2026-10-05/geometry-physical/` |
| Period candidate 5.859 ns, WNS +0.177 / WHS +0.122 | `2026-10-06/fmax/` |
| Block synth pair 5017→8459 LUT, routed WNS +0.427 / WHS +0.060 | `2026-10-08/block-runs/physical/` |
| 3519 checks; GCC13/Clang18 3514; 886 block events | `2026-10-08/README.md` |
| Full formal 27 PROVED + 8 required FAIL; cover 15 BOUNDED_PASS + 1 PROVED | `2026-10-08/block-runs/formal/` |
| Extended 37 PROVED + 10 required FAIL + 1 UNKNOWN; DIV4 later PROVED | `2026-10-09/block-formal/` (dirty tree, final run pending) |
| Resident traffic 1856 B / 26 vs 49976 B / 2072 | `2026-10-07/block-api-traffic/` |

Command: `make doc` (repository root). Exit status 0. 12 pages.
Diagnostics in `main.log`/`main.blg`: 0 undefined references/citations,
0 overfull and 0 underfull boxes, 0 BibTeX warnings. Hashes of every report
source in `source-sha256.txt`; PDF hash in `pdf-sha256.txt`.

Content corrections: shift semantics (SHL logical, SHR arithmetic, 4-bit
count), configuration-word bit layout incl. reserved bits, stale Vivado data
of 23 September replaced, stale software count (1049) replaced, "six
commands" vs seven-row table, abstract ratio computed from the CSV (2.0–4.0×,
up to +9 % bytes). New: evidence map, resident-block section with frame
figure and cost table, formal-verification subsection, geometry table/chart,
evidence-limits table, board acceptance plan. Style: Times math (mathptmx)
matching IEEEtran text, TeX Gyre Heros in figures, txtt monospace, single
link colour, shaded table headers. The tracked `doc/main.pdf` was NOT
replaced (T4/A06 distribution decision pending). No board test: NOT_RUN.
