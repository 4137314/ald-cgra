# LaTeX report and isolated unit previews. Run from doc/ with -f doc.mk.
# latexmk tracks recursive \input dependencies through .fls; a change in a
# nested part cannot be missed by a fixed-depth Make wildcard.
LATEXMK ?= latexmk
PYTHON  ?= python3
MAIN     = main
BUILD    = build
CC      ?= cc

FIGURES := $(patsubst figures/%/figure.tex,%,$(wildcard figures/*/figure.tex))
TABLES  := $(patsubst tables/%/table.tex,%,$(wildcard tables/*/table.tex))
SECTIONS := $(filter-out sections,$(basename $(notdir $(wildcard src/*.tex))))
DATA_OUTPUTS = $(BUILD)/wire-table.tex $(BUILD)/wire-ratios.csv
SCALING_OUTPUTS = $(BUILD)/scaling-repeat.csv $(BUILD)/scaling-length.csv \
	$(BUILD)/scaling-geometry.csv $(BUILD)/scaling-macros.tex
MODEL_OUTPUTS = $(BUILD)/latency-ratio.csv $(BUILD)/roofline-points.csv \
	$(BUILD)/roofline-ceiling.csv $(BUILD)/roofline-v3.csv \
	$(BUILD)/roofline-blocks.csv $(BUILD)/utilization.csv $(BUILD)/model-macros.tex
BOARD_OUTPUTS = $(BUILD)/board-turnaround.tex $(BUILD)/board-calls.tex \
	$(BUILD)/board-throughput.tex
ANALYSIS_OUTPUTS = $(BUILD)/ana-geometry.csv $(BUILD)/ana-time.csv $(BUILD)/ana-time-v3.csv \
	$(BUILD)/ana-reuse.csv $(BUILD)/ana-reuse-n.csv $(BUILD)/ana-macros.tex
ALL_DATA = $(DATA_OUTPUTS) $(SCALING_OUTPUTS) $(MODEL_OUTPUTS) $(BOARD_OUTPUTS) $(ANALYSIS_OUTPUTS)

.DEFAULT_GOAL := all
.PHONY: all clean measure measure-scaling measure-analysis supplement figures tables sections part help

all: $(ALL_DATA)
	$(LATEXMK) -pdf -interaction=nonstopmode -halt-on-error -file-line-error -output-directory=$(BUILD) $(MAIN).tex

# Both outputs are one generation step, including under make -j (GNU make 4.3+).
$(DATA_OUTPUTS) &: data/wire-cost.csv data/wire-cost.json tools/wire_data.py
	$(PYTHON) tools/wire_data.py render

# Scaling counts (emulator) and the models derived from archived counters.
$(SCALING_OUTPUTS) &: data/scaling.csv data/scaling.json data/wire-cost.csv tools/scaling_data.py
	$(PYTHON) tools/scaling_data.py render
$(MODEL_OUTPUTS) &: data/wire-cost.csv data/scaling.csv tools/model_data.py | $(SCALING_OUTPUTS)
	$(PYTHON) tools/model_data.py
# Manuscript analyses: archived counts re-verified against closed forms.
$(ANALYSIS_OUTPUTS) &: data/analysis/counts.csv data/analysis/provenance.json data/wire-cost.csv tools/analysis_data.py
	$(PYTHON) tools/analysis_data.py render
# Board figures: real plots once data/board/*.csv has rows, else NOT RUN.
$(BOARD_OUTPUTS) &: $(wildcard data/board/*.csv) data/wire-cost.csv tools/board_data.py
	$(PYTHON) tools/board_data.py

figures: $(addprefix figure-,$(FIGURES))
tables: $(addprefix table-,$(TABLES))
sections: $(addprefix section-,$(SECTIONS))

# These targets always consult latexmk; only changed input graphs recompile.
.PHONY: $(addprefix figure-,$(FIGURES)) $(addprefix table-,$(TABLES)) $(addprefix section-,$(SECTIONS))
$(addprefix figure-,$(FIGURES)): figure-%: $(ALL_DATA)
	$(PYTHON) tools/preview.py figure $* --latexmk "$(LATEXMK)"
$(addprefix table-,$(TABLES)): table-%: $(ALL_DATA)
	$(PYTHON) tools/preview.py table $* --latexmk "$(LATEXMK)"
$(addprefix section-,$(SECTIONS)): section-%: $(ALL_DATA)
	$(PYTHON) tools/preview.py section $* --latexmk "$(LATEXMK)"

# Example: make -f doc.mk part UNIT=protocol/fused-execute
part: $(ALL_DATA)
	$(PYTHON) tools/preview.py part "$(UNIT)" --latexmk "$(LATEXMK)"

# Explicit measurement step; report and previews use the archived CSV.
measure:
	$(MAKE) -C ../lib -f lib.mk PROFILE=release static
	mkdir -p $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -I../lib/include tools/measure_wire.c ../lib/build/libcgra.a -o $(BUILD)/measure_wire
	$(PYTHON) tools/wire_data.py record --binary $(BUILD)/measure_wire --compiler "$(CC)"

measure-scaling:
	$(MAKE) -C ../lib -f lib.mk PROFILE=release static
	mkdir -p $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -I../lib/include tools/measure_scaling.c ../lib/build/libcgra.a -o $(BUILD)/measure_scaling
	$(PYTHON) tools/scaling_data.py record --binary $(BUILD)/measure_scaling --compiler "$(CC)"

# Supplementary material: needs the paper's .aux for P- cross-references.
supplement: all
	$(LATEXMK) -pdf -interaction=nonstopmode -halt-on-error -file-line-error -output-directory=$(BUILD) supplement.tex

# Deliberate re-measurement for the manuscript. The library is compiled from
# the unmodified lib/ sources into $(BUILD)/analysis-lib only; nothing outside
# doc/build and the NEW data/analysis/ files is written. Refuses to overwrite.
measure-analysis:
	rm -rf $(BUILD)/analysis-lib && mkdir -p $(BUILD)/analysis-lib
	for f in ../lib/src/*.c; do $(CC) -std=c11 -O2 -I../lib/include -I../lib/src -c $$f -o $(BUILD)/analysis-lib/$$(basename $$f .c).o || exit 1; done
	ar rcs $(BUILD)/analysis-lib/libcgra.a $(BUILD)/analysis-lib/*.o
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -I../lib/include tools/measure_analysis.c $(BUILD)/analysis-lib/libcgra.a -o $(BUILD)/measure_analysis
	$(PYTHON) tools/analysis_data.py record --binary $(BUILD)/measure_analysis --library $(BUILD)/analysis-lib/libcgra.a --compiler "$(CC)"

help:
	@echo 'Full report: make -f doc.mk'
	@echo 'Figures: $(addprefix figure-,$(FIGURES))'
	@echo 'Tables: $(addprefix table-,$(TABLES))'
	@echo 'Sections: $(addprefix section-,$(SECTIONS))'
	@echo 'Part: make -f doc.mk part UNIT=protocol/fused-execute'
	@echo 'All previews: make -f doc.mk -j4 figures tables sections'

clean:
	$(LATEXMK) -C -output-directory=$(BUILD) $(MAIN).tex 2>/dev/null || true
	rm -rf $(BUILD)
