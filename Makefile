# Top-level orchestrator. Each component keeps its own <dir>/<dir>.mk fragment;
# this file just delegates so the fragments stay usable standalone
# (e.g. `make -C hw -f hw.mk sim`).
#
#   make            build the host library and the cgra CLI
#   make sim        run the GHDL testbenches
#   make lib        build the host library (libcgra.a, with sim: emulator)
#   make sw         build the cgra CLI
#   make test       run the software test suite (CLI against the emulator)
#   make bit        build the FPGA bitstream (needs Vivado)   [BOARD=basys3]
#   make prog       program the FPGA (Vivado hw_server)
#   make doc        build the LaTeX documentation
#   make clean

BOARD ?= basys3

MAKE_HW  = $(MAKE) -C hw  -f hw.mk
MAKE_LIB = $(MAKE) -C lib -f lib.mk
MAKE_SW  = $(MAKE) -C sw  -f sw.mk
MAKE_DOC = $(MAKE) -C doc -f doc.mk

.PHONY: all sim wave lib sw test bit prog prog-ofl doc docs clean

all: lib sw

sim:
	$(MAKE_HW) sim

wave:
	$(MAKE_HW) wave

lib:
	$(MAKE_LIB)

sw: lib
	$(MAKE_SW)

test: sw
	$(MAKE_SW) test

bit:
	$(MAKE_HW) bit BOARD=$(BOARD)

prog:
	$(MAKE_HW) prog BOARD=$(BOARD)

prog-ofl:
	$(MAKE_HW) prog-ofl BOARD=$(BOARD)

doc:
	$(MAKE_DOC)

# All documentation: LaTeX report, CLI man/info, library API man page.
docs: doc
	$(MAKE_SW) doc
	$(MAKE_LIB) man

clean:
	$(MAKE_HW) clean
	$(MAKE_LIB) clean
	$(MAKE_SW) clean
	$(MAKE_DOC) clean
