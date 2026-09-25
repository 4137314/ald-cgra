# lib/lib.mk - libcgra: static + shared library, unit tests, docs.
#
#   make -f lib.mk                    build build/libcgra.a and build/libcgra.so*
#   make -f lib.mk PROFILE=asan test  build+run unit tests under ASan+UBSan
#   make -f lib.mk PROFILE=debug ...   -O0 -g3, assertions on
#   make -f lib.mk test               run the assert.h unit tests (structured)
#   make -f lib.mk valgrind           run the unit tests under valgrind
#   make -f lib.mk analyze            gcc -fanalyzer static analysis
#   make -f lib.mk man                lint + render the man page
#   make -f lib.mk clean
#
# Parallel-safe: build with `make -f lib.mk -j`.

CC     ?= cc
AR     ?= ar
GROFF  ?= groff

VERSION   = 0.1.0
SOVERSION = 0

BUILD = build$(if $(filter release,$(PROFILE)),,/$(PROFILE))

# --- strict, structured diagnostics: one warning philosophy everywhere ------
WARN = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
       -Wcast-qual -Wcast-align -Wwrite-strings -Wundef -Wpointer-arith \
       -Wstrict-prototypes -Wmissing-prototypes -Wdouble-promotion -Wformat=2 \
       -fdiagnostics-color=auto

# --- build profiles (PROFILE=release|debug|asan|ubsan) ----------------------
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

CFLAGS  ?=
CFLAGS  += -std=c11 $(WARN) $(OPT) -Iinclude -MMD -MP
LDFLAGS += $(filter -fsanitize=% -pg,$(OPT))

SRCS    = src/cgra.c src/kernels.c src/emu.c src/serve.c
OBJS    = $(SRCS:src/%.c=$(BUILD)/%.o)
PICOBJS = $(SRCS:src/%.c=$(BUILD)/%.pic.o)

STATIC  = $(BUILD)/libcgra.a
SONAME  = libcgra.so.$(SOVERSION)
SOFILE  = libcgra.so.$(VERSION)
SHARED  = $(BUILD)/$(SOFILE)

MAN3    = doc/libcgra.3

.PHONY: all static shared test valgrind analyze man clean
all: static shared

static: $(STATIC)
shared: $(SHARED)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.pic.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(STATIC): $(OBJS)
	$(AR) rcs $@ $^

# Shared object with a proper soname + the usual dev/runtime symlinks.
$(SHARED): $(PICOBJS)
	$(CC) -shared -Wl,-soname,$(SONAME) $(LDFLAGS) -o $@ $^
	ln -sf $(SOFILE) $(BUILD)/$(SONAME)
	ln -sf $(SOFILE) $(BUILD)/libcgra.so

# --- unit tests: assert.h, TAP-style output, non-zero exit on failure -------
# Force assertions on regardless of PROFILE so `make PROFILE=release test`
# still checks invariants.
TESTBIN = $(BUILD)/test_cgra
TRANSPORTBIN = $(BUILD)/test_transport
KERNELBIN = $(BUILD)/test_kernels
FAULTBIN = $(BUILD)/test_faults

# Export the same CC/CFLAGS as compilation, without building the library.
.PHONY: compdb
compdb: | $(BUILD)
	$(file >$(BUILD)/compdb-flags.txt,$(CC) $(CFLAGS))
	python3 ../scripts/gen-compdb.py fragment $(BUILD)/compile_commands.json \
	    $(BUILD)/compdb-flags.txt $(SRCS) --tests \
	    $(addprefix test/,$(addsuffix .c,$(notdir $(TESTBIN) $(TRANSPORTBIN) $(KERNELBIN) $(FAULTBIN))))

test: $(TESTBIN) $(TRANSPORTBIN) $(KERNELBIN) $(FAULTBIN)
	./$(TESTBIN)
	./$(TRANSPORTBIN)
	./$(KERNELBIN)
	./$(FAULTBIN)

$(TESTBIN): test/test_cgra.c $(OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -UNDEBUG $< $(OBJS) $(LDFLAGS) -o $@

$(TRANSPORTBIN): test/test_transport.c $(OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -UNDEBUG $< $(OBJS) $(LDFLAGS) -o $@

$(KERNELBIN): test/test_kernels.c $(STATIC) | $(BUILD)
	$(CC) $(CFLAGS) -UNDEBUG $< $(STATIC) $(LDFLAGS) -o $@

$(FAULTBIN): test/test_faults.c $(STATIC) | $(BUILD)
	$(CC) $(CFLAGS) -UNDEBUG $< $(STATIC) $(LDFLAGS) -Wl,--wrap=calloc,--wrap=read,--wrap=__read_chk -o $@

valgrind: $(TESTBIN)
	valgrind --error-exitcode=99 --leak-check=full --track-origins=yes -q ./$(TESTBIN)

# Deep static analysis (the compiler's own path-sensitive checker).
analyze: | $(BUILD)
	mkdir -p $(BUILD)/analyze
	@for src in $(SRCS); do \
	    $(CC) -std=c11 -O1 -Iinclude -fanalyzer -Wall -Wextra -Werror \
	        -c "$$src" -o "$(BUILD)/analyze/$$(basename "$$src" .c).o" || exit 1; \
	done

man: | $(BUILD)
	$(GROFF) -man -z $(MAN3)
	$(GROFF) -man -Tutf8 $(MAN3) > $(BUILD)/libcgra.3.txt

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d) $(PICOBJS:.o=.d) $(BUILD)/test_cgra.d $(BUILD)/test_transport.d $(BUILD)/test_kernels.d $(BUILD)/test_faults.d
