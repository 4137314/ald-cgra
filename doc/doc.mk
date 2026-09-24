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

.DEFAULT_GOAL := all
.PHONY: all clean measure figures tables sections part help

all: $(DATA_OUTPUTS)
	$(LATEXMK) -pdf -interaction=nonstopmode -halt-on-error -file-line-error -output-directory=$(BUILD) $(MAIN).tex

# Both outputs are one generation step, including under make -j (GNU make 4.3+).
$(DATA_OUTPUTS) &: data/wire-cost.csv data/wire-cost.json tools/wire_data.py
	$(PYTHON) tools/wire_data.py render

figures: $(addprefix figure-,$(FIGURES))
tables: $(addprefix table-,$(TABLES))
sections: $(addprefix section-,$(SECTIONS))

# These targets always consult latexmk; only changed input graphs recompile.
.PHONY: $(addprefix figure-,$(FIGURES)) $(addprefix table-,$(TABLES)) $(addprefix section-,$(SECTIONS))
$(addprefix figure-,$(FIGURES)): figure-%: $(DATA_OUTPUTS)
	$(PYTHON) tools/preview.py figure $* --latexmk "$(LATEXMK)"
$(addprefix table-,$(TABLES)): table-%: $(DATA_OUTPUTS)
	$(PYTHON) tools/preview.py table $* --latexmk "$(LATEXMK)"
$(addprefix section-,$(SECTIONS)): section-%: $(DATA_OUTPUTS)
	$(PYTHON) tools/preview.py section $* --latexmk "$(LATEXMK)"

# Example: make -f doc.mk part UNIT=protocol/fused-execute
part: $(DATA_OUTPUTS)
	$(PYTHON) tools/preview.py part "$(UNIT)" --latexmk "$(LATEXMK)"

# Explicit measurement step; report and previews use the archived CSV.
measure:
	$(MAKE) -C ../lib -f lib.mk PROFILE=release static
	mkdir -p $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -I../lib/include tools/measure_wire.c ../lib/build/libcgra.a -o $(BUILD)/measure_wire
	$(PYTHON) tools/wire_data.py record --binary $(BUILD)/measure_wire --compiler "$(CC)"

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
