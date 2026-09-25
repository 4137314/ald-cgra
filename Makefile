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
export pkgdatadir

# Keep conventional paths prefix-relative, but honour explicit directory
# overrides in the installed pkg-config file. DESTDIR is staging only.
pc_libdir = $(libdir)
ifeq ($(libdir),$(PREFIX)/lib)
pc_libdir = $${exec_prefix}/lib
endif
pc_includedir = $(includedir)
ifeq ($(includedir),$(PREFIX)/include)
pc_includedir = $${prefix}/include
endif

# POSIX shell quoting for installation paths (including spaces/apostrophes).
sh_quote = '$(subst ','"'"',$(1))'
escape_sed = $(subst |,\|,$(subst &,\&,$(subst \,\\,$(1))))

INSTALL      ?= install
INSTALL_DATA  = $(INSTALL) -m644
INSTALL_PROG  = $(INSTALL) -m755
SOVERSION     = 0
SOFILE        = libcgra.so.$(VERSION)

.PHONY: all release sim wave lib sw test bench gprof perf callgrind profile \
        synth fmax bit sta prog prog-ofl doc docs \
        install uninstall install-strip test-install test-compdb check-deps dist compdb clean

all: lib sw

# Host compilation database, using each component's real compiler/profile flags.
COMPDB_BUILD = build$(if $(filter release,$(PROFILE)),,/$(PROFILE))
compdb:
	$(MAKE_LIB) compdb
	$(MAKE_SW) compdb
	python3 scripts/gen-compdb.py merge compile_commands.json \
	    lib/$(COMPDB_BUILD)/compile_commands.json sw/$(COMPDB_BUILD)/compile_commands.json

test-compdb:
	python3 scripts/test_compdb.py

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

# Copies working sources to a temporary directory; never installs system-wide.
test-install:
	python3 sw/test/test_install.py

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
	$(MAKE_SW) PROFILE=release info
	# binary
	$(INSTALL) -d $(call sh_quote,$(DESTDIR)$(bindir))
	$(INSTALL_PROG) sw/build/cgra $(call sh_quote,$(DESTDIR)$(bindir)/cgra)
	# libraries (static + shared with soname symlinks)
	$(INSTALL) -d $(call sh_quote,$(DESTDIR)$(libdir))
	$(INSTALL_DATA) lib/build/libcgra.a $(call sh_quote,$(DESTDIR)$(libdir)/libcgra.a)
	$(INSTALL_PROG) lib/build/$(SOFILE) $(call sh_quote,$(DESTDIR)$(libdir)/$(SOFILE))
	ln -sf $(SOFILE) $(call sh_quote,$(DESTDIR)$(libdir)/libcgra.so.$(SOVERSION))
	ln -sf $(SOFILE) $(call sh_quote,$(DESTDIR)$(libdir)/libcgra.so)
	# header
	$(INSTALL) -d $(call sh_quote,$(DESTDIR)$(includedir))
	$(INSTALL_DATA) lib/include/cgra.h $(call sh_quote,$(DESTDIR)$(includedir)/cgra.h)
	# pkg-config file (actual install directories; no DESTDIR)
	$(INSTALL) -d $(call sh_quote,$(DESTDIR)$(pkgconfigdir))
	sed -e $(call sh_quote,s|@PREFIX@|$(call escape_sed,$(PREFIX))|g) -e $(call sh_quote,s|@VERSION@|$(VERSION)|g) \
	    -e $(call sh_quote,s|@LIBDIR@|$(call escape_sed,$(pc_libdir))|g) \
	    -e $(call sh_quote,s|@INCLUDEDIR@|$(call escape_sed,$(pc_includedir))|g) \
	    lib/cgra.pc.in > $(call sh_quote,$(DESTDIR)$(pkgconfigdir)/cgra.pc)
	# man pages (1 = CLI, 3 = library, 5 = config format)
	$(INSTALL) -d $(call sh_quote,$(DESTDIR)$(mandir)/man1) $(call sh_quote,$(DESTDIR)$(mandir)/man3) $(call sh_quote,$(DESTDIR)$(mandir)/man5)
	$(INSTALL_DATA) sw/doc/cgra.1     $(call sh_quote,$(DESTDIR)$(mandir)/man1/cgra.1)
	$(INSTALL_DATA) lib/doc/libcgra.3 $(call sh_quote,$(DESTDIR)$(mandir)/man3/libcgra.3)
	$(INSTALL_DATA) sw/doc/cgra.5     $(call sh_quote,$(DESTDIR)$(mandir)/man5/cgra.5)
	# info manual
	$(INSTALL) -d $(call sh_quote,$(DESTDIR)$(infodir))
	$(INSTALL_DATA) sw/build/cgra.info $(call sh_quote,$(DESTDIR)$(infodir)/cgra.info)
	# bash completion
	$(INSTALL) -d $(call sh_quote,$(DESTDIR)$(completiondir))
	$(INSTALL_DATA) sw/completions/cgra.bash $(call sh_quote,$(DESTDIR)$(completiondir)/cgra)
	# runtime standard library (.cgra)
	$(INSTALL) -d $(call sh_quote,$(DESTDIR)$(pkgdatadir)/stdlib)
	$(INSTALL_DATA) sw/config/stdlib/*.cgra $(call sh_quote,$(DESTDIR)$(pkgdatadir)/stdlib/)
	@printf '%s\n' $(call sh_quote,installed cgra $(VERSION) under $(DESTDIR)$(PREFIX))
	@echo "run 'ldconfig' if you installed the shared library to a system dir"

install-strip: install
	strip $(call sh_quote,$(DESTDIR)$(bindir)/cgra)

uninstall:
	rm -f  $(call sh_quote,$(DESTDIR)$(bindir)/cgra)
	rm -f  $(call sh_quote,$(DESTDIR)$(libdir)/libcgra.a)
	rm -f  $(call sh_quote,$(DESTDIR)$(libdir)/$(SOFILE)) $(call sh_quote,$(DESTDIR)$(libdir)/libcgra.so.$(SOVERSION)) $(call sh_quote,$(DESTDIR)$(libdir)/libcgra.so)
	rm -f  $(call sh_quote,$(DESTDIR)$(includedir)/cgra.h)
	rm -f  $(call sh_quote,$(DESTDIR)$(pkgconfigdir)/cgra.pc)
	rm -f  $(call sh_quote,$(DESTDIR)$(mandir)/man1/cgra.1) $(call sh_quote,$(DESTDIR)$(mandir)/man3/libcgra.3) $(call sh_quote,$(DESTDIR)$(mandir)/man5/cgra.5)
	rm -f  $(call sh_quote,$(DESTDIR)$(infodir)/cgra.info)
	rm -f  $(call sh_quote,$(DESTDIR)$(completiondir)/cgra)
	rm -rf $(call sh_quote,$(DESTDIR)$(pkgdatadir))
	@printf '%s\n' $(call sh_quote,uninstalled cgra from $(DESTDIR)$(PREFIX))

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
