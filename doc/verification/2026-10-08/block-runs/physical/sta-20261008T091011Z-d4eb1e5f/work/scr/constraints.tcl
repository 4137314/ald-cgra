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
#   cgra_timing_constraints <clock_period_ns> [step_div] [rows] [cols]
#
# step_div is the datapath multicycle factor and MUST equal the G_STEP_DIV
# generic the RTL was synthesised with (the flow passes both from one knob,
# make STEP_DIV=...). Default 2.
#
source [file join [file dirname [info script]] timing_checks.tcl]

proc cgra_register_set {pattern expected label} {
    set actual [get_cells -hier -filter "REF_NAME =~ FD* && NAME =~ $pattern"]
    if {[lsort $actual] ne [lsort $expected]} {
        error "$label register coverage mismatch: expected [llength $expected], found [llength $actual] ($pattern)"
    }
    return $actual
}

# Check every expected bit and PE, not only a nonempty wildcard. The six
# reserved CFG bits [31:26] have no consumers and are removed by synthesis.
proc cgra_datapath_registers {rows cols {blocks false}} {
    if {![string is integer -strict $rows] || ![string is integer -strict $cols] ||
        $rows < 1 || $cols < 1 || $rows * $cols > 16} {error "invalid constraint geometry"}
    set pe {}; set cfg {}; set west {}; set north {}
    for {set r 0} {$r < $rows} {incr r} {
        for {set c 0} {$c < $cols} {incr c} {
            for {set bit 0} {$bit < 16} {incr bit} {
                lappend pe [format {u_array/gen_rows[%d].gen_cols[%d].u_pe/r_reg[%d]} $r $c $bit]
            }
            for {set bit 0} {$bit < 26} {incr bit} {
                lappend cfg [format {u_ctrl/cfg_r_reg[%d][%d]} [expr {$r * $cols + $c}] $bit]
            }
        }
        for {set bit 0} {$bit < 16} {incr bit} {
            lappend west [format {u_ctrl/west_r_reg[%d][%d]} $r $bit]
        }
    }
    for {set c 0} {$c < $cols} {incr c} {
        for {set bit 0} {$bit < 16} {incr bit} {
            lappend north [format {u_ctrl/north_r_reg[%d][%d]} $c $bit]
        }
    }
    set dst [cgra_register_set *u_pe/r_reg* $pe {PE output}]
    set src $dst
    foreach pattern {*cfg_r_reg* *west_r_reg* *north_r_reg*} expected [list $cfg $west $north] {
        set src [concat $src [cgra_register_set $pattern $expected $pattern]]
    }
    if {$blocks} {
        set operands {}
        set src [concat $src [cgra_register_set *engine/owned_reg* {blocks.u_block/engine/owned_reg} {block configuration select}]]
        foreach bank {a b} {
            for {set bit 0} {$bit < 16} {incr bit} {
                lappend operands [format {blocks.u_block/engine/operand_%s_reg[%d]} $bank $bit]
            }
        }
        set src [concat $src [cgra_register_set *engine/operand_*_reg* $operands {block operands}]]
    }
    return [list $src $dst]
}

proc cgra_timing_constraints {period_ns {step_div 2} {rows 4} {cols 4} {blocks false}} {
    cgra_timing_args $period_ns $step_div
    set period_ns [cgra_clock_period $period_ns]
    foreach port {clk uart_rx_i uart_tx_o btn_rst {led[*]}} {
        cgra_nonempty [get_ports $port] "port $port"
    }
    # ---- system clock (board oscillator pin) ----------------------------
    create_clock -period $period_ns -name sys_clk [get_ports clk]
    set actual [get_property PERIOD [cgra_nonempty [get_clocks sys_clk] {system clock}]]
    if {abs($actual - $period_ns) > 0.000001} {error "clock period differs from applied constraint: $actual vs $period_ns"}

    # ---- asynchronous I/O: excluded from timing -------------------------
    set_false_path -from [get_ports uart_rx_i] -to [cgra_nonempty [get_pins u_rx/rx_s1_reg/D] {RX first stage D}]
    set_false_path -to   [get_ports uart_tx_o]
    set_false_path -from [get_ports btn_rst] -to [cgra_nonempty [get_pins rst_s1_reg/D] {reset first stage D}]
    set_false_path -to   [get_ports {led[*]}]

    # ---- CGRA datapath: N-cycle multicycle path (N = step_div) ----------
    # The controller issues 'step' once every G_STEP_DIV clocks (cgra_ctrl.vhd
    # S_RUN), so every PE result register (u_pe/r_reg) captures at most once
    # every G_STEP_DIV cycles. S_RUN's initial guard also separates a fused
    # EXEC reset from the first capture by at least G_STEP_DIV cycles; this is
    # checked alongside consecutive captures by tb_protocol_diff.
    # Give the datapath -- neighbour/self/config/edge
    # operands through the operand mux, DSP multiply, accumulate and op mux --
    # G_STEP_DIV clock periods without adding a pipeline stage. Closure must
    # still be checked on the actual implemented netlist. step_div MUST equal
    # the synthesised G_STEP_DIV; the exception is justified by capture/data
    # stability, not by the need to obtain a positive timing report.
    # Match only the flip-flops (REF_NAME FDRE/FDSE/...), NOT the combinational
    # LUTs Vivado names r_reg[N]_i_M after synthesis -- not valid endpoints.
    lassign [cgra_datapath_registers $rows $cols $blocks] src_cells dst_cells
    if { $step_div >= 2 } {
        # Launch clocks to data D endpoints only: Vivado requires register C
        # startpoints; Q is not a valid -from pin. Cell -to endpoints would
        # also relax CE/reset pins, which must retain single-cycle checks.
        set src [get_pins -of_objects $src_cells -filter {REF_PIN_NAME == C}]
        set dst [get_pins -of_objects $dst_cells -filter {REF_PIN_NAME == D}]
        if {[llength $src] != [llength $src_cells] || [llength $dst] != [llength $dst_cells]} {
            error "missing datapath C/D pins"
        }
        set_multicycle_path $step_div              -setup -from $src -to $dst
        set_multicycle_path [expr {$step_div - 1}] -hold  -from $src -to $dst
    }
    return $period_ns
}
