# Report second pass — analyses, figures, bibliography (15 October 2026)

## New measurement (emulator, counts only)

`make -C doc -f doc.mk measure-scaling` — exit 0. Builds `lib` (release),
compiles `doc/tools/measure_scaling.c` with GCC 15.2.0 against the public
API, runs it on the clockless emulator and records `doc/data/scaling.{csv,json}`
(provenance copy here: `scaling-provenance.json`; base commit 0f8a54b, dirty
tree, source hashes inside). 134 points: repeated dot (v2/v3/resident,
n = 16..1024, r = 1..64) and five kernels × five 16-PE shapes × v2/v3.
Every point is checked against an independent scalar result before output.
hardware: NOT_RUN. No time is measured.

`tools/scaling_data.py render` re-derives every transaction count from closed
forms. First attempts FAILED on 16x1 matvec and then 16x1 conv: the
measurement was right, the assertion omitted the documented skip of
zero-weight column tiles (input b[i] = i%5-2 has zeros; with C = 1 a tile
is a single element). The skip rule was added to the check; no data were
changed. The 4x4 points equal `data/wire-cost.csv` exactly (transactions
and bytes), which also confirms that the archived Table VIII data are
reproduced by the current library.

## Models (no measurement)

`tools/model_data.py`: T = B*tb + J*tau ratios, break-even tau* =
tb*(B3-B2)/(J2-J3): dot 174 us, matvec 58 us, conv 58 us (vector/scan never
slower); roofline with 16 PE x 100 MHz / D=2 = 800 Mop/s and 11 520 B/s;
PE utilisation per mapping and shape.

## Board placeholders

`doc/data/board/*.csv` are header-only; `tools/board_data.py` renders NOT RUN
panels. The populated path was exercised once with temporary synthetic rows
(rendered correctly), then the header-only files were restored byte for
byte; no synthetic data remain or appear in the report.

## Report build

`make doc` — exit 0, 14 pages; 0 undefined references, 0 overfull/underfull
boxes, 0 BibTeX warnings (`main.log`, `main.blg`). Figures redrawn: FSM
(true state machine with RTL names, watchdog states checked in
cgra_ctrl.vhd lines 181-195, unknown opcode NACK line 228), systolic grid
(TikZ grid step was in cm, misaligned), PE datapath (crossing control
wires), stack (block path, horizontal link). New: Figs. 7-9, 11, 14.
Bibliography: 42 entries, 12 added (surveys, roofline, RPC duplicate
suppression, end-to-end, CRC, mutation, differential testing, ASan,
benchmarking methodology, metastability, model checking).
