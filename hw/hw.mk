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
.DEFAULT_GOAL := all
TOP      ?= cgra_top
GHDL     ?= ghdl
VIVADO   ?= vivado
OFL      ?= openFPGALoader
TCLSH    ?= tclsh
PERIOD   ?= 10.000
# Datapath multicycle factor (clocks per array step). Drives BOTH the RTL
# generic G_STEP_DIV and the XDC multicycle number, so they can never disagree.
STEP_DIV ?= 2
# Device geometry is fixed at elaboration; host kernels discover it via ID.
MESH_ROWS ?= 4
MESH_COLS ?= 4
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
       sim/tb_matvec.vhd \
       sim/tb_array_diff.vhd \
       sim/tb_protocol_diff.vhd \
       sim/tb_kernel_uart.vhd \
       sim/tb_array_geometry.vhd

# Self-checking testbench top-levels, run in order of increasing scope.
TB_UNITS = tb_pe tb_uart tb_cgra_top tb_matvec tb_array_diff
STEP_DIVS ?= 1 2 3 4
MESH_SHAPES ?= 1x1 1x4 4x1 2x3 3x2 4x4 8x8
UART_SHAPES ?= 2x3 3x2
DEVICE_SHAPES ?= 1x1 1x16 16x1 2x3 3x2 2x8 8x2 4x4
KERNEL_VECTORS = $(addprefix build/kernels_,$(addsuffix .vec,$(DEVICE_SHAPES)))
PROTOCOL_VECTORS = $(addprefix build/protocol_,$(addsuffix .vec,$(DEVICE_SHAPES)))

DIFF_GEN = $(GHDL_DIR)/gen_diff_vectors
MODEL_SRCS = ../lib/src/cgra.c ../lib/src/kernels.c ../lib/src/emu.c
MODEL_HEADERS = ../lib/include/cgra.h ../lib/src/emu.h ../lib/src/buffers.h
SIM_CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror

# Build the reference from source in the simulation directory. Standalone HDL
# runs must not pick up a stale library or one built with a different profile.
$(DIFF_GEN): sim/gen_diff_vectors.c $(MODEL_SRCS) $(MODEL_HEADERS) | $(GHDL_DIR)
	$(CC) $(SIM_CFLAGS) -I../lib/include $< $(MODEL_SRCS) -o $@

build/array_diff.vec: $(DIFF_GEN)
	./$(DIFF_GEN) $@.tmp
	mv $@.tmp $@

$(GHDL_DIR)/gen_protocol_vectors: sim/gen_protocol_vectors.c ../lib/src/emu.c $(MODEL_HEADERS) | $(GHDL_DIR)
	$(CC) $(SIM_CFLAGS) -I../lib/include -I../lib/src $< ../lib/src/emu.c -o $@

build/protocol_diff.vec: $(GHDL_DIR)/gen_protocol_vectors
	./$(GHDL_DIR)/gen_protocol_vectors $@.tmp
	mv $@.tmp $@

build/protocol_%.vec: $(GHDL_DIR)/gen_protocol_vectors
	./$(GHDL_DIR)/gen_protocol_vectors $@.tmp $(word 1,$(subst x, ,$*)) $(word 2,$(subst x, ,$*))
	mv $@.tmp $@

$(GHDL_DIR)/gen_kernel_vectors: sim/gen_kernel_vectors.c $(MODEL_SRCS) $(MODEL_HEADERS) \
        ../sw/compile.c ../sw/compile.h ../sw/dsl.c ../sw/dsl.h ../sw/test/kernel_cases.h | $(GHDL_DIR)
	$(CC) $(SIM_CFLAGS) -I../lib/include -I../lib/src -I../sw $< $(MODEL_SRCS) ../sw/compile.c ../sw/dsl.c -o $@

build/kernels_%.vec: $(GHDL_DIR)/gen_kernel_vectors
	./$(GHDL_DIR)/gen_kernel_vectors $@.tmp $(word 1,$(subst x, ,$*)) $(word 2,$(subst x, ,$*))
	mv $@.tmp $@

VIVADO_DIR = build/vivado$(if $(filter 4x4,$(MESH_ROWS)x$(MESH_COLS)),,/$(MESH_ROWS)x$(MESH_COLS))
BIT = $(VIVADO_DIR)/$(TOP)_$(BOARD).bit

.PHONY: all sim test-scripts lint wave synth fmax bit sta prog prog-ofl clean

all: sim

# Offline checks of timing gates with synthetic Vivado replies.
test-scripts:
	$(TCLSH) test/test_timing.tcl

$(GHDL_DIR):
	mkdir -p $(GHDL_DIR)

sim: build/array_diff.vec build/protocol_diff.vec $(PROTOCOL_VECTORS) $(KERNEL_VECTORS) $(addprefix build/kernels_,$(addsuffix .vec,$(UART_SHAPES))) | $(GHDL_DIR)
	$(GHDL) -a $(GHDL_FLAGS) $(SRCS) $(TBS)
	@for tb in $(TB_UNITS); do \
	    echo "== $$tb =="; \
	    $(GHDL) -e $(GHDL_FLAGS) $$tb || exit 1; \
	    $(GHDL) -r $(GHDL_FLAGS) $$tb --assert-level=error || exit 1; \
	done
	$(GHDL) -e $(GHDL_FLAGS) tb_protocol_diff
	@for div in $(STEP_DIVS); do \
	    $(GHDL) -r $(GHDL_FLAGS) tb_protocol_diff -gG_STEP_DIV=$$div --assert-level=error || exit 1; \
	done
	@for shape in $(DEVICE_SHAPES); do \
	    rows=$${shape%x*}; cols=$${shape#*x}; \
	    $(GHDL) -r $(GHDL_FLAGS) tb_protocol_diff -gG_ROWS=$$rows -gG_COLS=$$cols \
	        -gG_VECTOR_FILE=build/protocol_$$shape.vec --assert-level=error || exit 1; \
	    $(GHDL) -r $(GHDL_FLAGS) tb_protocol_diff -gG_ROWS=$$rows -gG_COLS=$$cols \
	        -gG_VECTOR_FILE=build/kernels_$$shape.vec --assert-level=error || exit 1; \
	done
	$(GHDL) -e $(GHDL_FLAGS) tb_kernel_uart
	@for shape in $(UART_SHAPES); do \
	    rows=$${shape%x*}; cols=$${shape#*x}; \
	    $(GHDL) -r $(GHDL_FLAGS) tb_kernel_uart -gG_ROWS=$$rows -gG_COLS=$$cols \
	        -gG_VECTOR_FILE=build/kernels_$$shape.vec --assert-level=error || exit 1; \
	done
	$(GHDL) -e $(GHDL_FLAGS) tb_array_geometry
	@for shape in $(MESH_SHAPES); do \
	    rows=$${shape%x*}; cols=$${shape#*x}; \
	    $(GHDL) -r $(GHDL_FLAGS) tb_array_geometry -gG_ROWS=$$rows -gG_COLS=$$cols --assert-level=error || exit 1; \
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
	$(VIVADO) -mode batch -nolog -nojournal -source scr/synth.tcl -tclargs $(BOARD) $(PERIOD) $(STEP_DIV) $(MESH_ROWS) $(MESH_COLS)

# Binary-search the maximum frequency the design closes at and print the
# limiting path (the thing to optimise next). Appends the result to
# build/vivado/fmax_validated_history.csv for comparison of checked periods.
fmax:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/fmax.tcl -tclargs $(BOARD) $(FMAX_LO) $(FMAX_HI) $(FMAX_IT) $(STEP_DIV) $(MESH_ROWS) $(MESH_COLS)

bit:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/build.tcl -tclargs $(BOARD) $(PERIOD) $(STEP_DIV) $(MESH_ROWS) $(MESH_COLS)

# Static timing analysis gate: exits non-zero unless setup+hold timing is met,
# under the applied constraints. Physical board validation is separate.
sta:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/timing.tcl -tclargs $(BOARD) $(PERIOD) $(STEP_DIV) $(MESH_ROWS) $(MESH_COLS)

prog:
	$(VIVADO) -mode batch -nolog -nojournal -source scr/program.tcl -tclargs $(BIT)

prog-ofl:
	$(OFL) -b $(BOARD) $(BIT)

clean:
	rm -rf build *.o *.cf tb_pe tb_cgra_top e~*
