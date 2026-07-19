# Top-level orchestrator. Each component keeps its own <dir>/<dir>.mk fragment;
# this file just delegates so the fragments stay usable standalone
# (e.g. `make -C hw -f hw.mk sim`).
#
#   make               build the host library and the cgra CLI (optimized)
#   make sim           run the GHDL testbenches
#   make test          run all C tests (unit + smoke + sanitizers optional)
#   make docs          build every document (report, man pages, info manual)
#   make install       install to $(PREFIX) [DESTDIR=... PREFIX=/usr/local]
#   make uninstall     remove an installation
#   make check-deps    report which build/runtime dependencies are present
#   make dist          make a source tarball
#   make synth         synthesis-only RTL gate (needs Vivado)  [BOARD=nexys_a7]
#   make fmax          search the maximum closing clock frequency (needs Vivado)
#   make bit           build the FPGA bitstream (needs Vivado)  [BOARD=nexys_a7]
#   make sta           static timing analysis gate (needs Vivado)
#   make clean
#
# Build profile propagates to the sub-makefiles, e.g. `make PROFILE=asan test`.

BOARD   ?= nexys_a7
PROFILE ?= release
VERSION  = 0.1.0

MAKE_HW  = $(MAKE) -C hw  -f hw.mk
MAKE_LIB = $(MAKE) -C lib -f lib.mk PROFILE=$(PROFILE)
MAKE_SW  = $(MAKE) -C sw  -f sw.mk  PROFILE=$(PROFILE)
MAKE_DOC = $(MAKE) -C doc -f doc.mk

# ---- GNU-style install locations (override PREFIX/DESTDIR as usual) --------
PREFIX        ?= /usr/local
DESTDIR       ?=
bindir         = $(PREFIX)/bin
libdir         = $(PREFIX)/lib
includedir     = $(PREFIX)/include
datarootdir    = $(PREFIX)/share
mandir         = $(datarootdir)/man
infodir        = $(datarootdir)/info
pkgconfigdir   = $(libdir)/pkgconfig
completiondir  = $(datarootdir)/bash-completion/completions
pkgdatadir     = $(datarootdir)/cgra

INSTALL      ?= install
INSTALL_DATA  = $(INSTALL) -m644
INSTALL_PROG  = $(INSTALL) -m755
SOVERSION     = 0
SOFILE        = libcgra.so.$(VERSION)

.PHONY: all release sim wave lib sw test bench gprof perf callgrind profile \
        synth fmax bit sta prog prog-ofl doc docs \
        install uninstall install-strip check-deps dist compdb clean

all: lib sw

# clangd compilation database (replaces compile_flags.txt). Regenerate after
# adding a source or changing include flags. Git-ignored (absolute paths).
compdb:
	sh scripts/gen-compdb.sh

# Optimised build used by `install` (PROFILE=release => -O3 -DNDEBUG).
release:
	$(MAKE) all PROFILE=release

sim:
	$(MAKE_HW) sim

wave:
	$(MAKE_HW) wave

lib:
	$(MAKE_LIB)

sw: lib
	$(MAKE_SW)

# All C tests: library unit tests + CLI unit + smoke, in the chosen profile.
test: lib
	$(MAKE_LIB) test
	$(MAKE_SW) test

# Stress-benchmark the whole .cgra standard library on a device (structured
# output). DEV=sim (default) dry-runs on the emulator; DEV=auto uses the FPGA
# discovered by `cgra probe`. Override SIZES/REPEAT as needed.
DEV    ?= sim
SIZES  ?= 256 1024 4096
REPEAT ?= 50
bench: sw
	$(MAKE_SW) bench DEV="$(DEV)" SIZES="$(SIZES)" REPEAT="$(REPEAT)"

# C performance analysis of the CLI driving the emulator (CPU-bound): pick one.
gprof perf callgrind profile: lib
	$(MAKE_SW) $@

synth:
	$(MAKE_HW) synth BOARD=$(BOARD)

fmax:
	$(MAKE_HW) fmax BOARD=$(BOARD)

bit:
	$(MAKE_HW) bit BOARD=$(BOARD)

sta:
	$(MAKE_HW) sta BOARD=$(BOARD)

prog:
	$(MAKE_HW) prog BOARD=$(BOARD)

prog-ofl:
	$(MAKE_HW) prog-ofl BOARD=$(BOARD)

doc:
	$(MAKE_DOC)

# All documentation: LaTeX report, CLI man/info, library man page.
docs: doc
	$(MAKE_SW) doc
	$(MAKE_LIB) man

# ---- installation ---------------------------------------------------------
# Builds the optimised binaries and the info manual, then lays everything out
# under $(DESTDIR)$(PREFIX) following the FHS. Man page sources install as-is.
install: release
	$(MAKE_SW) info
	# binary
	$(INSTALL) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROG) sw/build/cgra $(DESTDIR)$(bindir)/cgra
	# libraries (static + shared with soname symlinks)
	$(INSTALL) -d $(DESTDIR)$(libdir)
	$(INSTALL_DATA) lib/build/libcgra.a $(DESTDIR)$(libdir)/libcgra.a
	$(INSTALL_PROG) lib/build/$(SOFILE) $(DESTDIR)$(libdir)/$(SOFILE)
	ln -sf $(SOFILE) $(DESTDIR)$(libdir)/libcgra.so.$(SOVERSION)
	ln -sf $(SOFILE) $(DESTDIR)$(libdir)/libcgra.so
	# header
	$(INSTALL) -d $(DESTDIR)$(includedir)
	$(INSTALL_DATA) lib/include/cgra.h $(DESTDIR)$(includedir)/cgra.h
	# pkg-config file (prefix/version substituted in)
	$(INSTALL) -d $(DESTDIR)$(pkgconfigdir)
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@VERSION@|$(VERSION)|g' \
	    lib/cgra.pc.in > $(DESTDIR)$(pkgconfigdir)/cgra.pc
	# man pages (1 = CLI, 3 = library, 5 = config format)
	$(INSTALL) -d $(DESTDIR)$(mandir)/man1 $(DESTDIR)$(mandir)/man3 $(DESTDIR)$(mandir)/man5
	$(INSTALL_DATA) sw/doc/cgra.1     $(DESTDIR)$(mandir)/man1/cgra.1
	$(INSTALL_DATA) lib/doc/libcgra.3 $(DESTDIR)$(mandir)/man3/libcgra.3
	$(INSTALL_DATA) sw/doc/cgra.5     $(DESTDIR)$(mandir)/man5/cgra.5
	# info manual
	$(INSTALL) -d $(DESTDIR)$(infodir)
	$(INSTALL_DATA) sw/build/cgra.info $(DESTDIR)$(infodir)/cgra.info
	# bash completion
	$(INSTALL) -d $(DESTDIR)$(completiondir)
	$(INSTALL_DATA) sw/completions/cgra.bash $(DESTDIR)$(completiondir)/cgra
	# runtime standard library (.cgra)
	$(INSTALL) -d $(DESTDIR)$(pkgdatadir)/stdlib
	$(INSTALL_DATA) sw/config/stdlib/*.cgra $(DESTDIR)$(pkgdatadir)/stdlib/
	@echo "installed cgra $(VERSION) under $(DESTDIR)$(PREFIX)"
	@echo "run 'ldconfig' if you installed the shared library to a system dir"

install-strip: install
	strip $(DESTDIR)$(bindir)/cgra

uninstall:
	rm -f  $(DESTDIR)$(bindir)/cgra
	rm -f  $(DESTDIR)$(libdir)/libcgra.a
	rm -f  $(DESTDIR)$(libdir)/$(SOFILE) $(DESTDIR)$(libdir)/libcgra.so.$(SOVERSION) $(DESTDIR)$(libdir)/libcgra.so
	rm -f  $(DESTDIR)$(includedir)/cgra.h
	rm -f  $(DESTDIR)$(pkgconfigdir)/cgra.pc
	rm -f  $(DESTDIR)$(mandir)/man1/cgra.1 $(DESTDIR)$(mandir)/man3/libcgra.3 $(DESTDIR)$(mandir)/man5/cgra.5
	rm -f  $(DESTDIR)$(infodir)/cgra.info
	rm -f  $(DESTDIR)$(completiondir)/cgra
	rm -rf $(DESTDIR)$(pkgdatadir)
	@echo "uninstalled cgra from $(DESTDIR)$(PREFIX)"

# ---- dependency probe (mirrors the flake's toolchain) ---------------------
# The library itself needs only a C11 compiler + libc; the rest is optional
# (readline for the shell, and the doc/HDL toolchain). pkg-config resolves the
# library deps the flake pins; the tools are probed with command -v.
check-deps:
	@echo "== required =="; \
	for t in $(CC) cc gcc make ar; do command -v $$t >/dev/null 2>&1 && echo "  ok    $$t" && break; done; \
	command -v pkg-config >/dev/null 2>&1 && echo "  ok    pkg-config" || echo "  MISS  pkg-config"; \
	echo "== optional: cli =="; \
	pkg-config --exists readline 2>/dev/null && echo "  ok    readline $$(pkg-config --modversion readline)" || echo "  MISS  readline (cgra shell falls back to fgets)"; \
	echo "== optional: docs =="; \
	for t in makeinfo groff latexmk; do command -v $$t >/dev/null 2>&1 && echo "  ok    $$t" || echo "  MISS  $$t"; done; \
	echo "== optional: hardware =="; \
	for t in ghdl gtkwave vivado openFPGALoader valgrind; do command -v $$t >/dev/null 2>&1 && echo "  ok    $$t" || echo "  MISS  $$t"; done; \
	echo "(nix develop provides every 'MISS' except Vivado)"

# ---- source tarball -------------------------------------------------------
dist:
	@test -d .git || { echo "dist needs a git checkout"; exit 1; }
	git archive --format=tar.gz --prefix=cgra-$(VERSION)/ -o cgra-$(VERSION).tar.gz HEAD
	@echo "wrote cgra-$(VERSION).tar.gz"

clean:
	$(MAKE_HW) clean
	$(MAKE_LIB) clean
	$(MAKE_SW) clean
	$(MAKE_DOC) clean
