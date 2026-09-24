# constraints.tcl — board-independent TIMING constraints for the CGRA.
#
# Sourced by scr/{synth,build,timing,fmax}.tcl. The board .xdc files carry only
# the PHYSICAL constraints (pin placement, I/O standards, config); everything
# that depends on the clock PERIOD lives here so a single knob (the argument to
# cgra_timing_constraints) drives the whole flow -- that is what lets scr/fmax.tcl
# sweep the frequency without editing any file.
#
# Apply AFTER synth_design so the cell-based multicycle exception resolves
# against the synthesised netlist (get_cells matches real leaf registers).
#
#   cgra_timing_constraints <clock_period_ns> [step_div]
#
# step_div is the datapath multicycle factor and MUST equal the G_STEP_DIV
# generic the RTL was synthesised with (the flow passes both from one knob,
# make STEP_DIV=...). Default 2.
#
source [file join [file dirname [info script]] timing_checks.tcl]

proc cgra_timing_constraints {period_ns {step_div 2}} {
    cgra_timing_args $period_ns $step_div
    foreach port {clk uart_rx_i uart_tx_o btn_rst {led[*]}} {
        cgra_nonempty [get_ports $port] "port $port"
    }
    # ---- system clock (board oscillator pin) ----------------------------
    create_clock -period $period_ns -name sys_clk [get_ports clk]

    # ---- asynchronous I/O: excluded from timing -------------------------
    set_false_path -from [get_ports uart_rx_i]
    set_false_path -to   [get_ports uart_tx_o]
    set_false_path -from [get_ports btn_rst]
    set_false_path -to   [get_ports {led[*]}]

    # ---- CGRA datapath: N-cycle multicycle path (N = step_div) ----------
    # The controller issues 'step' once every G_STEP_DIV clocks (cgra_ctrl.vhd
    # S_RUN), so every PE result register (u_pe/r_reg) captures at most once
    # every G_STEP_DIV cycles. S_RUN's initial guard also separates a fused
    # EXEC reset from the first capture by at least G_STEP_DIV cycles; this is
    # checked alongside consecutive captures by tb_protocol_diff.
    # Give the datapath -- neighbour/self/config/edge
    # operands through the operand mux, DSP multiply, accumulate and op mux --
    # the full G_STEP_DIV clock periods it needs. This closes timing WITHOUT
    # pipelining the DSP, so one step still equals one result and the fabric
    # stays bit-identical to emu.c. step_div MUST equal the synthesised
    # G_STEP_DIV; dropping it (or step_div=1 at a fast clock) re-opens a large
    # setup violation on the 16x16 single-step MAC path.
    # Match only the flip-flops (REF_NAME FDRE/FDSE/...), NOT the combinational
    # LUTs Vivado names r_reg[N]_i_M after synthesis -- not valid endpoints.
    if { $step_div >= 2 } {
        set dst [cgra_nonempty [get_cells -hier -filter {REF_NAME =~ FD* && NAME =~ *u_pe/r_reg*}] "PE result registers"]
        set src $dst
        foreach pattern {*cfg_r_reg* *west_r_reg* *north_r_reg*} {
            set cells [cgra_nonempty [get_cells -hier -filter "REF_NAME =~ FD* && NAME =~ $pattern"] $pattern]
            set src [concat $src $cells]
        }
        set_multicycle_path $step_div              -setup -from $src -to $dst
        set_multicycle_path [expr {$step_div - 1}] -hold  -from $src -to $dst
    }
}
