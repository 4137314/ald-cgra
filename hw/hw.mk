# hw/Makefile — simulation with GHDL, bitstream/programming with Vivado batch mode.
#
# Targets:
#   make sim              run all testbenches (GHDL)
#   make lint             analyse the RTL only (syntax/elaboration check)
#   make wave             run tb_cgra_top dumping build/tb_cgra_top.ghw (view with gtkwave)
#   make bit              build the bitstream (Vivado batch, no GUI)  [BOARD=basys3|nexys_a7]
#   make sta              static timing analysis gate: fail unless timing is met
#   make prog             program the FPGA via Vivado hardware server
#   make prog-ofl         program the FPGA via openFPGALoader (no Vivado needed)
#   make clean

BOARD  ?= basys3
TOP    ?= cgra_top
GHDL   ?= ghdl
VIVADO ?= vivado
OFL    ?= openFPGALoader

GHDL_DIR   = build/ghdl
GHDL_FLAGS = --std=08 --workdir=$(GHDL_DIR)

SRCS = rtl/cgra_pkg.vhd \
       rtl/uart_rx.vhd \
       rtl/uart_tx.vhd \
       rtl/pe.vhd \
       rtl/cgra_array.vhd \
       rtl/cgra_ctrl.vhd \
       rtl/cgra_top.vhd

TBS  = sim/tb_pe.vhd \
       sim/tb_uart.vhd \
       sim/tb_cgra_top.vhd \
       sim/tb_matvec.vhd

# Self-checking testbench top-levels, run in order of increasing scope.
TB_UNITS = tb_pe tb_uart tb_cgra_top tb_matvec

BIT = build/vivado/$(TOP)_$(BOARD).bit

.PHONY: all sim lint wave bit sta prog prog-ofl clean

all: sim

$(GHDL_DIR):
	mkdir -p $(GHDL_DIR)

sim: | $(GHDL_DIR)
	$(GHDL) -a $(GHDL_FLAGS) $(SRCS) $(TBS)
	@for tb in $(TB_UNITS); do \
	    echo "== $$tb =="; \
	    $(GHDL) -e $(GHDL_FLAGS) $$tb || exit 1; \
	    $(GHDL) -r $(GHDL_FLAGS) $$tb --assert-level=error || exit 1; \
	done

# Analyse the synthesisable RTL on its own: catches syntax/elaboration errors
# without running any testbench.
lint: | $(GHDL_DIR)
	$(GHDL) -a $(GHDL_FLAGS) $(SRCS)
	@echo "lint: RTL analysed clean"

wave: | $(GHDL_DIR)
	$(GHDL) -a $(GHDL_FLAGS) $(SRCS) $(TBS)
	$(GHDL) -e $(GHDL_FLAGS) tb_cgra_top
	$(GHDL) -r $(GHDL_FLAGS) tb_cgra_top --wave=build/tb_cgra_top.ghw

bit:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/build.tcl -tclargs $(BOARD)

# Static timing analysis gate: exits non-zero unless setup+hold timing is met,
# so `make sta` passing means the bitstream will meet timing on the board.
sta:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/timing.tcl -tclargs $(BOARD)

prog:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/program.tcl -tclargs $(BIT)

prog-ofl:
	$(OFL) -b $(BOARD) $(BIT)

clean:
	rm -rf build *.o *.cf tb_pe tb_cgra_top e~*
