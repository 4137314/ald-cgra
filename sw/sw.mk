# sw/sw.mk — builds the cgra CLI against lib/.

CC       ?= cc
CFLAGS   ?= -O2 -g
CFLAGS   += -std=c11 -Wall -Wextra -I../lib/include
MAKEINFO ?= makeinfo
GROFF    ?= groff

BUILD = build
LIB   = ../lib/build/libcgra.a
BIN   = $(BUILD)/cgra
OBJS  = $(BUILD)/main.o $(BUILD)/dsl.o $(BUILD)/compile.o

# Documentation sources: the modular texinfo manual and the man pages.
TEXISRC = doc/cgra.texi doc/overview.texi doc/invocation.texi \
          doc/commands.texi doc/configuration.texi doc/stdlib.texi \
          doc/dataflow.texi doc/protocol.texi doc/environment.texi \
          doc/examples.texi doc/files.texi
MANPAGES = doc/cgra.1 doc/cgra.5

.PHONY: all lib test doc info man clean

all: $(BIN)

lib:
	$(MAKE) -C ../lib -f lib.mk

$(LIB): lib

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c dsl.h compile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS) $(LIB)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) -o $@

# Exercises the whole stack (DSL -> compiler -> library -> emulator), no FPGA.
test: $(BIN)
	./$(BIN) selftest
	@echo "--- CLI smoke tests (device sim) ---"
	@echo "1 2 3 4 5 6 7 8" | ./$(BIN) run add -d sim --b "10 20 30 40 50 60 70 80"
	@echo "-3 -1 0 2 5" | ./$(BIN) run relu -d sim
	./$(BIN) run muli -d sim --imm 10 --a "1 2 3 4" --io hex
	./$(BIN) pipe saxpy -d sim --a "1 2 3 4"
	./$(BIN) matvec -d sim -m "1 2 3 4  5 6 7 8  9 10 11 12  13 14 15 16" --x "1 1 1 1"
	./$(BIN) scan -d sim --a "3 1 4 1 5 9 2 6"
	./$(BIN) scan -d sim --op max --a "3 1 4 1 5 9 2 6"
	./$(BIN) conv -d sim -m "1 2 3" --x "4 5 6 7 8"
	./$(BIN) run add -d sim:flaky --a "1 2 3 4" --b "10 20 30 40"   # retry recovers
	./$(BIN) run add -d sim --a "1 2 3" --b "10 20 30" --json
	CGRA_PATH=config/stdlib ./$(BIN) run square -d sim --a "2 3 4 5"   # stdlib via CGRA_PATH
	./$(BIN) show relu -v | head -1
	./$(BIN) show add >/dev/null && echo "show ok"

# Documentation: GNU info manual (from the modular texinfo) and the man pages
# cgra.1 (CLI) and cgra.5 (the .cgra file format).
doc: info man

info: $(BUILD)/cgra.info

$(BUILD)/cgra.info: $(TEXISRC) | $(BUILD)
	$(MAKEINFO) -o $@ doc/cgra.texi

man: | $(BUILD)
	@for p in $(MANPAGES); do \
	    echo "  lint   $$p"; $(GROFF) -man -z $$p; \
	    echo "  render $$p"; $(GROFF) -man -Tutf8 $$p > $(BUILD)/$$(basename $$p).txt; \
	done

clean:
	rm -rf $(BUILD)
