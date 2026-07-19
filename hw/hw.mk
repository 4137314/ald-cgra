# hw/Makefile — simulation with GHDL, bitstream/programming with Vivado batch mode.
#
# Targets:
#   make sim              run all testbenches (GHDL)
#   make wave             run tb_cgra_top dumping build/tb_cgra_top.ghw (view with gtkwave)
#   make bit              build the bitstream (Vivado batch, no GUI)  [BOARD=basys3|nexys_a7]
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
       sim/tb_cgra_top.vhd \
       sim/tb_matvec.vhd

BIT = build/vivado/$(TOP)_$(BOARD).bit

.PHONY: all sim wave bit prog prog-ofl clean

all: sim

$(GHDL_DIR):
	mkdir -p $(GHDL_DIR)

sim: | $(GHDL_DIR)
	$(GHDL) -a $(GHDL_FLAGS) $(SRCS) $(TBS)
	$(GHDL) -e $(GHDL_FLAGS) tb_pe
	$(GHDL) -e $(GHDL_FLAGS) tb_cgra_top
	$(GHDL) -e $(GHDL_FLAGS) tb_matvec
	$(GHDL) -r $(GHDL_FLAGS) tb_pe
	$(GHDL) -r $(GHDL_FLAGS) tb_cgra_top
	$(GHDL) -r $(GHDL_FLAGS) tb_matvec

wave: | $(GHDL_DIR)
	$(GHDL) -a $(GHDL_FLAGS) $(SRCS) $(TBS)
	$(GHDL) -e $(GHDL_FLAGS) tb_cgra_top
	$(GHDL) -r $(GHDL_FLAGS) tb_cgra_top --wave=build/tb_cgra_top.ghw

bit:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/build.tcl -tclargs $(BOARD)

prog:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/program.tcl -tclargs $(BIT)

prog-ofl:
	$(OFL) -b $(BOARD) $(BIT)

clean:
	rm -rf build *.o *.cf tb_pe tb_cgra_top e~*
