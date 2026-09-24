# Working on the report

## Read and edit the smallest relevant unit

- `main.tex` assembles front matter, `src/sections.tex`, and references.
- `src/sections.tex` owns section order. Most `src/NAME.tex` files only own
  the section heading/label and the ordered inputs in `src/NAME/`.
- Each `src/NAME/TOPIC.tex` owns one topic. Introduction and conclusion are
  already short enough to remain single files.
- `figures/NAME/diagram.tex` owns TikZ/pgfplots geometry and drawing labels.
  `figures/NAME/figure.tex` owns the float, sizing, caption and reference label.
- `tables/NAME/table.tex` owns one complete table. The measured table body and
  chart data are generated from `data/wire-cost.csv`; edit the measurement
  source and deliberately regenerate data when changing that experiment.
- `config/` owns shared styling/macros. Do not duplicate its preamble in a unit.
- `bib/bib.bib` is the only bibliography source. Never edit a generated `.bbl`.
- `backmatter/references.tex` owns the bibliography's placement/balancing.

Use `doc/STRUCTURE.md` for the topic map. For a figure-only change, start with
its two files and the relevant shared style; reading every section is usually
unnecessary. Edit section order or shared styles only when the change needs it.
Do not create a new file for each sentence: a topic, diagram, caption/float or
whole table is the intended unit. Keep environments/braces complete per file.
All `\input` paths are relative to `doc/`, including those in nested files.

## Focused build loop (commands from the repository root)

```
make -C doc -f doc.mk figure-pe
make -C doc -f doc.mk table-wire-sizes
make -C doc -f doc.mk section-protocol
make -C doc -f doc.mk part UNIT=protocol/fused-execute
make doc
```

Previews are at `doc/build/previews/KIND/NAME/preview.pdf`. Figure/table
previews include the real caption and use the report's sizing and fonts.
Section/part previews use the IEEE column layout. The last full report's
`.aux` supplies external reference numbers when available: rebuild the full
report when labels/order change. Preview numbering is local; it is not a
publication artifact. On a clean tree cross-section references can be `??`
until the first full build. A preview never silently launches a full build.

Use `make -C doc -f doc.mk help` for the discovered targets. `latexmk` tracks
nested inputs and skips unchanged work; each preview has its own output
folder, so parallel preview builds do not share auxiliary files. A single
`part` target accepts one UNIT; invoke the Python tool directly for multiple
parts in independent shells if necessary.

After a drawing/layout edit, render that PDF with `pdftoppm` and inspect it.
Before completing a change, build the full report and check missing references,
BibTeX diagnostics and overfull boxes. No new software test suite is needed
for moving prose, but check text equivalence when reorganizing sources.

## Evidence and persistence

Update the applicable task in `../TASKS.md` and record meaningful verification
under `verification/YYYY-MM-DD/`. Existing logs describe their historical
source snapshot; do not rewrite old evidence to make it match moved files.
Do not infer hardware timing, DSP mapping, area or speedup from emulator/RTL
simulation. Distinguish actual Vivado CLI runs from mocked Tcl flow tests and
from physical board measurements. Preserve the source/input hashes associated
with measured data. `main.pdf` is a historical tracked copy; the current build
output is `build/main.pdf` (distribution cleanup remains T4).
