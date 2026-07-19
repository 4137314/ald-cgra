# sw/sw.mk - the cgra CLI: build, unit + smoke tests, docs.
#
#   make -f sw.mk                     build build/cgra (release)
#   make -f sw.mk PROFILE=asan test   build+test under ASan+UBSan
#   make -f sw.mk unit                assert.h unit tests (DSL + compiler)
#   make -f sw.mk smoke               CLI smoke tests against the sim: emulator
#   make -f sw.mk test                unit + smoke
#   make -f sw.mk valgrind            unit tests under valgrind
#   make -f sw.mk analyze             gcc -fanalyzer static analysis
#   make -f sw.mk doc                 info manual + man pages
#   make -f sw.mk clean
#
# readline is optional: if pkg-config finds it, `cgra shell` gets line editing
# and history; otherwise it falls back to plain fgets (KISS, no hard dep).

CC       ?= cc
MAKEINFO ?= makeinfo
GROFF    ?= groff

BUILD  = build
LIBDIR = ../lib
LIB    = $(LIBDIR)/build/libcgra.a

WARN = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
       -Wcast-qual -Wcast-align -Wwrite-strings -Wundef -Wpointer-arith \
       -Wstrict-prototypes -Wmissing-prototypes -Wdouble-promotion -Wformat=2 \
       -fdiagnostics-color=auto

PROFILE     ?= release
OPT.release  = -O3 -DNDEBUG
OPT.debug    = -O0 -g3 -fno-omit-frame-pointer
OPT.asan     = -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
OPT.ubsan    = -O1 -g -fno-omit-frame-pointer -fsanitize=undefined
OPT.gprof    = -O2 -g -pg                          # gmon.out for gprof
OPT.perf     = -O2 -g -fno-omit-frame-pointer      # frame pointers for perf/callgrind
OPT          = $(OPT.$(PROFILE))
ifeq ($(strip $(OPT)),)
$(error unknown PROFILE '$(PROFILE)'; use release|debug|asan|ubsan|gprof|perf)
endif

# Optional readline (line editing + history for `cgra shell`).
HAVE_READLINE := $(shell pkg-config --exists readline 2>/dev/null && echo 1)
ifeq ($(HAVE_READLINE),1)
  RL_CFLAGS := -DHAVE_READLINE $(shell pkg-config --cflags readline)
  RL_LIBS   := $(shell pkg-config --libs readline)
endif

CFLAGS  ?=
CFLAGS  += -std=c11 $(WARN) $(OPT) -I. -I$(LIBDIR)/include $(RL_CFLAGS) -MMD -MP
LDFLAGS += $(filter -fsanitize=% -pg,$(OPT))
LDLIBS  += $(RL_LIBS)

BIN  = $(BUILD)/cgra
OBJS = $(BUILD)/main.o $(BUILD)/dsl.o $(BUILD)/compile.o

# Documentation sources: the modular texinfo manual and the man pages.
TEXISRC = doc/cgra.texi doc/overview.texi doc/invocation.texi \
          doc/commands.texi doc/configuration.texi doc/stdlib.texi \
          doc/dataflow.texi doc/protocol.texi doc/environment.texi \
          doc/examples.texi doc/files.texi
MANPAGES = doc/cgra.1 doc/cgra.5

# Benchmark pipeline knobs (see scripts/cgra-bench.sh).
DEV    ?= sim
SIZES  ?= 256 1024 4096
REPEAT ?= 50

.PHONY: all lib unit test smoke bench valgrind analyze gprof perf callgrind profile doc info man clean

all: $(BIN)

# Build libcgra with the SAME profile, so objects and sanitizers match.
lib:
	$(MAKE) -C $(LIBDIR) -f lib.mk PROFILE=$(PROFILE) static

$(LIB): lib

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS) $(LIB)
	$(CC) $(CFLAGS) $(OBJS) $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

# --- unit tests (assert.h): DSL parser + mode compiler + engines -----------
UNITBIN = $(BUILD)/test_sw
unit: $(UNITBIN)
	./$(UNITBIN)

$(UNITBIN): test/test_sw.c $(BUILD)/dsl.o $(BUILD)/compile.o $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -UNDEBUG $< $(BUILD)/dsl.o $(BUILD)/compile.o $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

# --- CLI smoke tests: exercise the whole stack (DSL->compiler->lib->emu) ----
smoke: $(BIN)
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

test: unit smoke

# --- stress-benchmark the whole .cgra standard library on a device ----------
# make bench DEV=sim|auto|/dev/ttyUSB1  SIZES="256 1024 4096"  REPEAT=50
bench: $(BIN)
	DEV="$(DEV)" SIZES="$(SIZES)" REPEAT="$(REPEAT)" CGRA="./$(BIN)" sh scripts/cgra-bench.sh

valgrind: $(UNITBIN)
	valgrind --error-exitcode=99 --leak-check=full --track-origins=yes -q ./$(UNITBIN)

analyze:
	$(CC) -std=c11 -I$(LIBDIR)/include $(RL_CFLAGS) -fanalyzer -Wall -Wextra -fsyntax-only main.c dsl.c compile.c

# --- C performance analysis: gprof / perf / callgrind -----------------------
# Each rebuilds the CLI with the right instrumentation and profiles it driving
# the emulator (CPU-bound, so the hotspots are the C code: protocol framing,
# checksums, the PE model). Override the workload with WORKLOAD=...
WORKLOAD ?= benchall -d sim --size 4096 --repeat 200

gprof:
	$(MAKE) -f sw.mk clean
	$(MAKE) -f sw.mk PROFILE=gprof
	./$(BIN) $(WORKLOAD) >/dev/null
	gprof ./$(BIN) gmon.out > $(BUILD)/gprof.txt
	@echo "wrote $(BUILD)/gprof.txt"; head -12 $(BUILD)/gprof.txt

perf:
	$(MAKE) -f sw.mk clean
	$(MAKE) -f sw.mk PROFILE=perf
	perf stat -- ./$(BIN) $(WORKLOAD) >/dev/null
	perf record -g -o $(BUILD)/perf.data -- ./$(BIN) $(WORKLOAD) >/dev/null
	perf report -i $(BUILD)/perf.data --stdio > $(BUILD)/perf.txt
	@echo "wrote $(BUILD)/perf.txt (perf report -i $(BUILD)/perf.data for TUI)"

callgrind:
	$(MAKE) -f sw.mk clean
	$(MAKE) -f sw.mk PROFILE=perf
	valgrind --tool=callgrind --callgrind-out-file=$(BUILD)/callgrind.out \
	    ./$(BIN) $(WORKLOAD) >/dev/null
	callgrind_annotate $(BUILD)/callgrind.out > $(BUILD)/callgrind.txt
	@echo "wrote $(BUILD)/callgrind.txt (kcachegrind $(BUILD)/callgrind.out for a GUI)"

profile: gprof callgrind perf

# --- documentation: GNU info manual + man pages cgra.1 (CLI) and cgra.5 -----
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

-include $(OBJS:.o=.d) $(BUILD)/test_sw.d
