# doc/doc.mk — LaTeX documentation build.
#
#   make -C doc -f doc.mk        build build/main.pdf
#   make -C doc -f doc.mk clean

LATEXMK ?= latexmk
MAIN     = main
BUILD    = build

.PHONY: all clean

all: $(BUILD)/$(MAIN).pdf

$(BUILD)/$(MAIN).pdf: $(MAIN).tex
	$(LATEXMK) -pdf -output-directory=$(BUILD) $(MAIN).tex

clean:
	$(LATEXMK) -C -output-directory=$(BUILD) $(MAIN).tex 2>/dev/null || true
	rm -rf $(BUILD)
