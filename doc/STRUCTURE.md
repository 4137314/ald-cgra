# Report editing map

## Assembly

```text
main.tex
├── frontmatter/{title,abstract,keywords}.tex
├── src/sections.tex                 section order
│   ├── src/introduction.tex         short self-contained section
│   ├── src/NAME.tex                 heading + ordered topic inputs
│   │   └── src/NAME/TOPIC.tex        one argument or subsection
│   │       ├── figures/ID/figure.tex caption, label, size, placement
│   │       │   └── diagram.tex      TikZ/pgfplots only
│   │       └── tables/ID/table.tex   complete table
│   └── src/conclusion.tex           short self-contained section
└── backmatter/references.tex
    └── bib/bib.bib + IEEEtran.bst
```

Paths inside `\input` are always relative to `doc/`. Splitting a paragraph or
an environment across files is unnecessary. A unit should contain enough
context to review its claim or diagram on its own.

## Where to edit

| Change | Start here | Related assets |
|---|---|---|
| CGRA literature / taxonomy | `src/background/{related-work,stationary-data}.tex` | `bib/bib.bib` |
| PE arithmetic / feedback | `src/architecture/processing-element.tex` | `figures/pe/` |
| Mesh wiring / boundary injection | `src/architecture/interconnect.tex` | `figures/array/` |
| Command formats | `src/protocol/commands.tex` | `tables/commands/` |
| Payload example | `src/protocol/wire-example.tex` | — |
| EXEC, retry and framing | `src/protocol/fused-execute.tex` | `figures/exec/` |
| Controller phases / recovery | `src/protocol/controller.tex` | `figures/fsm/` |
| Matrix mapping and host reduction | `src/dataflow/matrix-vector.tex` | `figures/systolic/` |
| Alternative schedules | `src/dataflow/forwarding.tex` | — |
| C API buffer/ownership contracts | `src/software/contracts.tex` | public header/man page outside doc/ |
| Host architecture | `src/software/{transport,dispatch,geometry,cli}.tex` | `figures/stack/` |
| Wire formulas | `src/analysis/{command-costs,kernel-counts,latency}.tex` | `tables/{wire-sizes,kernel-counts}/` |
| Multicycle timing | `src/implementation/multicycle.tex` | `figures/timing/` |
| Vivado evidence / limits | `src/implementation/{scope,timing-gates,physical-evaluation,validation}.tex` | archived Vivado reports |
| Functional evidence | `src/results/verification.tex` | `tables/verification/` |
| Wire-cost experiment | `src/results/wire-experiment.tex` | `tables/measured/`, `figures/cost/`, `data/wire-cost.*` |
| Title / summary | `frontmatter/` | PDF metadata in `config/hyperref.tex` |

## Commands (from the repository root)

```sh
make -C doc -f doc.mk figure-pe
make -C doc -f doc.mk table-commands
make -C doc -f doc.mk section-dataflow
make -C doc -f doc.mk part UNIT=protocol/fused-execute
make -C doc -f doc.mk -j4 figures tables sections
make doc
```

`make -C doc -f doc.mk help` lists all available named targets from the tree.
The corresponding artifacts are `doc/build/previews/figure/pe/preview.pdf`,
`.../table/commands/preview.pdf`, `.../section/dataflow/preview.pdf` and
`.../part/protocol/fused-execute/preview.pdf`. No preview driver is maintained
by hand: `tools/preview.py` generates it and `tools/preview-float.tex` crops
figures/tables using native pdfTeX. All use the same IEEE class and config.

For an image to inspect:

```sh
pdftoppm -singlefile -png -r 160 doc/build/previews/figure/pe/preview.pdf /tmp/cgra-pe
```

Each build has its own auxiliary files. `latexmk` follows the actual input
graph, including nested topics, captions and styles. Repeating an unchanged
preview does not rerun pdfLaTeX. External reference numbers come from the last
full build when present, while captions and headings in previews are numbered
locally. A final full build checks global placement, numbering and references.

An ordinary report/preview build uses archived wire data. Only
`make -C doc -f doc.mk measure` deliberately replaces the experiment CSV/JSON.
Both generated wire assets are one grouped Make target, preventing concurrent
preview jobs from racing on the same data. GNU make 4.3 or later is required.
