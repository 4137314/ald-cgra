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
supplement.tex                       separate PDF, src/supplement/*.tex
```

Paths inside `\input` are always relative to `doc/`. Splitting a paragraph or
an environment across files is unnecessary. A unit should contain enough
context to review its claim or diagram on its own.

## Where to edit

| Change | Start here | Related assets |
|---|---|---|
| Problem, research questions, contributions | `src/introduction.tex` | `frontmatter/` |
| Related work and comparison | `src/background/{related-work,stationary-data}.tex` | `tables/related/`, `bib/bib.bib` |
| PE, mesh, edge injection | `src/architecture/{overview,processing-element,interconnect}.tex` | `figures/{pe,array}/` |
| Command protocols and failure contracts | `src/protocol/{commands,contracts}.tex` | `tables/commands/`, `figures/exec/` |
| Resident-block extension and session semantics | `src/blocks/framing.tex` | `figures/frame/` |
| Mappings, counting conventions | `src/dataflow/{mappings,conventions}.tex` | `figures/systolic/` |
| Wire cost and time model | `src/analysis/{command-costs,latency}.tex` | `tables/{wire-sizes,kernel-counts}/` |
| Methodology and evidence classes | `src/method.tex` | `tables/verification/` |
| RQ1 fusion | `src/results/rq1.tex` | `tables/measured/`, `figures/{latency,time}/` |
| RQ2 geometry and useful work | `src/results/rq2.tex` | `figures/tradeoff/` |
| RQ3 residency, resources, verification effort | `src/results/rq3.tex` | `figures/reuse/`, `tables/block-cost/` |
| Proof-to-STA chain, Vivado results | `src/results/{chain,vivado-values}.tex` | `tables/{formal,vivado}/` |
| Limitations, artifact, conclusion | `src/{limits,artifact,conclusion}.tex` | — |
| Supplement (controller, software, extra views, board plan, chronology) | `supplement.tex`, `src/supplement/*.tex` | `figures/{fsm,stack,timing,geometry,cost,scaling,board}/` |
| Title / summary | `frontmatter/` | PDF metadata in `config/hyperref.tex` |

## Data and generated assets

| Data (archived, never rewritten by a build) | Renderer | Regenerate deliberately |
|---|---|---|
| `data/wire-cost.{csv,json}` | `tools/wire_data.py` | `make -C doc -f doc.mk measure` |
| `data/scaling.{csv,json}` | `tools/scaling_data.py` | `make -C doc -f doc.mk measure-scaling` |
| `data/analysis/{counts.csv,provenance.json}` | `tools/analysis_data.py` | `make -C doc -f doc.mk measure-analysis` |
| `data/board/*.csv` (header only: NOT RUN) | `tools/board_data.py` | fill from board measurements |

Each renderer checks the provenance hash and the closed-form counts before
writing `build/`. `tools/model_data.py` derives the latency-ratio model.
The supplement is built with `make -C doc -f doc.mk supplement` after the
paper, because it imports the paper's numbering (`xr-hyper`, prefix `P-`).

## Commands (from the repository root)

```sh
make -C doc -f doc.mk figure-pe
make -C doc -f doc.mk table-commands
make -C doc -f doc.mk section-dataflow
make -C doc -f doc.mk part UNIT=results/rq1
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
`make -C doc -f doc.mk measure` (Table VIII) and `measure-scaling`
(`data/scaling.*`) deliberately replace experiment CSV/JSON; the renderers
recheck provenance hashes and closed-form counts before drawing. Model
curves (`tools/model_data.py`) and board figures (`tools/board_data.py`, NOT
RUN while `data/board/*.csv` are header-only) are regenerated by every build.
Both generated wire assets are one grouped Make target, preventing concurrent
preview jobs from racing on the same data. GNU make 4.3 or later is required.
