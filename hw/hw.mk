# hw/Makefile — simulation with GHDL, bitstream/programming with Vivado batch mode.
#
# Targets:
#   make sim              run all testbenches (GHDL)
#   make lint             analyse the RTL only (syntax/elaboration check)
#   make wave             run tb_cgra_top dumping build/tb_cgra_top.ghw (view with gtkwave)
#   make synth            synthesis-only gate (Vivado): fail on error/critical warning  [BOARD=]
#   make fmax             search the maximum closing clock frequency + report the wall
#   make bit              build the bitstream (Vivado batch, no GUI)  [BOARD=nexys_a7|basys3]
#   make sta              static timing analysis gate: fail unless timing is met  [PERIOD=]
#   make prog             program the FPGA via Vivado hardware server
#   make prog-ofl         program the FPGA via openFPGALoader (no Vivado needed)
#   make clean
#
# Knobs: BOARD (nexys_a7=Nexys 4 DDR, default | basys3), PERIOD=<ns> clock target
# for bit/sta, FLOORPLAN=1 to apply scr/floorplan.tcl in bit/sta/fmax.

BOARD    ?= nexys_a7
TOP      ?= cgra_top
GHDL     ?= ghdl
VIVADO   ?= vivado
OFL      ?= openFPGALoader
PERIOD   ?= 10.000
# Datapath multicycle factor (clocks per array step). Drives BOTH the RTL
# generic G_STEP_DIV and the XDC multicycle number, so they can never disagree.
STEP_DIV ?= 2
# Fmax search window (ns) and iteration count.
FMAX_LO  ?= 5.0
FMAX_HI  ?= 10.0
FMAX_IT  ?= 6

GHDL_DIR   = build/ghdl
GHDL_FLAGS = --std=08 --workdir=$(GHDL_DIR)

SRCS = rtl/cgra_pkg.vhd \
       rtl/cgra_comp_pkg.vhd \
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

.PHONY: all sim lint wave synth fmax bit sta prog prog-ofl clean

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

# Synthesis-only gate: elaborate + synthesise the RTL for the target part and
# fail on any ERROR or CRITICAL WARNING, without running implementation. Much
# faster than `bit`/`sta` (~seconds of tool work) -- the first Vivado check to
# run after editing the RTL. Reports land in build/vivado/*_synth_$(BOARD).rpt.
synth:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/synth.tcl -tclargs $(BOARD) $(PERIOD) $(STEP_DIV)

# Binary-search the maximum frequency the design closes at and print the
# limiting path (the thing to optimise next). Appends the result to
# build/vivado/fmax_history.csv so successive runs show the improvement trail.
fmax:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/fmax.tcl -tclargs $(BOARD) $(FMAX_LO) $(FMAX_HI) $(FMAX_IT) $(STEP_DIV)

bit:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/build.tcl -tclargs $(BOARD) $(PERIOD) $(STEP_DIV)

# Static timing analysis gate: exits non-zero unless setup+hold timing is met,
# so `make sta` passing means the bitstream will meet timing on the board.
sta:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/timing.tcl -tclargs $(BOARD) $(PERIOD) $(STEP_DIV)

prog:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/program.tcl -tclargs $(BIT)

prog-ofl:
	$(OFL) -b $(BOARD) $(BIT)

clean:
	rm -rf build *.o *.cf tb_pe tb_cgra_top e~*
