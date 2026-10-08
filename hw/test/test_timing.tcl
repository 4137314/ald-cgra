# Run from hw/: tclsh test/test_timing.tcl
# Tests script decisions with synthetic Vivado answers, NOT an FPGA netlist.
source scr/timing_checks.tcl
set checks 0
proc check {condition message} {
    incr ::checks
    if {![uplevel 1 [list expr $condition]]} { error "FAIL: $message" }
    puts "ok timing $::checks - $message"
}
foreach period {0 -1 Inf NaN abc {}} {
    check {[catch {cgra_timing_args $period 2}]} "reject invalid period '$period'"
}
foreach div {0 -1 1.5 abc} {
    check {[catch {cgra_timing_args 10 $div}]} "reject invalid divider '$div'"
}
check {[cgra_clock_period 5.859375] == 5.859} "fractional-picosecond period is applied on an explicit 1 ps grid"
check {[cgra_clock_period 6.484375] == 6.484} "all sweep candidates use the same period precision"
check {[catch {cgra_clock_period 0.0001}]} "period rounding to zero is rejected"
check {[cgra_checked_frequency 8 1.5 0.1] == 125.0} "frequency uses actual period, not slack extrapolation"
foreach slacks {{-0.1 0} {0 -0.1} {NaN 0} {0 Inf} {{} 0}} {
    check {[catch {cgra_checked_frequency 8 {*}$slacks}]} "reject invalid/failing slack $slacks"
}

# Exercise the production scripts in isolated interpreters with mocked Vivado
# commands. The real Tcl source/control flow/report gate runs unchanged.
set tmp [file normalize [file join build timing-test-[pid]]]
file mkdir $tmp
set script_root [file normalize scr]
proc run_flow {script fault} {
    set child [interp create]
    interp eval $child [list set script_root $::script_root]
    interp eval $child [list set outdir $::tmp]
    interp eval $child [list set fault $fault]
    interp eval $child {
        set argc 0
        set argv {}
        if {$fault in {block_enabled missing_block_operand}} {
            set argc 6; set argv {nexys_a7 10 2 4 4 1}
        }
        set final 0
        set bit_written 0
        set checkpoints {}
        rename source real_source
        proc source {args} {
            set path [lindex $args end]
            if {[file tail $path] eq "structural_checks.tcl"} {
                # This suite tests flow decisions. Real netlist/CDC/placement
                # checks are executed by Vivado, not certified by this mock.
                proc cgra_physical_gate {args} {
                    if {$::fault eq "structural"} {error "synthetic failed structural check"}
                }
                return
            }
            if {[file tail $path] eq "geometry.tcl"} {
                uplevel 1 [list real_source {*}$args]
                proc cgra_geometry {args} { return [list 4 4 $::outdir] }
                return
            }
            uplevel 1 [list real_source {*}$args]
        }
        proc exit {code} { error "EXIT:$code" }
        foreach cmd {read_vhdl read_xdc synth_design open_checkpoint opt_design place_design route_design set_false_path} {
            proc $cmd {args} {}
        }
        proc create_clock {args} {set ::period [lindex $args [expr {[lsearch -exact $args -period]+1}]]}
        proc get_clocks {args} {return sys_clk}
        proc set_multicycle_path {args} {
            foreach key {-from -to} pin {C D} {
                set list [lindex $args [expr {[lsearch -exact $args $key] + 1}]]
                foreach object $list {
                    if {![string match */$pin $object]} {error "multicycle includes non-$pin pin: $object"}
                }
            }
        }
        proc phys_opt_design {args} { set ::final 1 }
        proc write_checkpoint {args} { lappend ::checkpoints [lindex $args end] }
        proc write_bitstream {args} { set ::bit_written 1 }
        proc get_msg_config {args} {return 0}
        proc get_ports {pattern} {
            if {$::fault eq "missing_port" && $pattern eq "clk"} { return {} }
            return [list $pattern]
        }
        proc get_cells {args} {
            if {$::fault eq "missing_cells"} { return {} }
            set cells {}
            for {set r 0} {$r < 4} {incr r} {
                for {set c 0} {$c < 4} {incr c} {
                    for {set b 0} {$b < 16} {incr b} {
                        lappend cells [format {u_array/gen_rows[%d].gen_cols[%d].u_pe/r_reg[%d]} $r $c $b]
                    }
                    for {set b 0} {$b < 26} {incr b} {
                        lappend cells [format {u_ctrl/cfg_r_reg[%d][%d]} [expr {4*$r+$c}] $b]
                    }
                }
                foreach edge {west north} {
                    for {set b 0} {$b < 16} {incr b} {
                        lappend cells [format {u_ctrl/%s_r_reg[%d][%d]} $edge $r $b]
                    }
                }
            }
            set pattern [lindex [lindex $args end] end]
            if {[info exists ::block_enable] && $::block_enable} {
                lappend cells blocks.u_block/engine/owned_reg
                foreach bank {a b} {
                    for {set b 0} {$b < 16} {incr b} {
                        if {$::fault eq "missing_block_operand" && $bank eq "b" && $b == 15} {continue}
                        lappend cells [format {blocks.u_block/engine/operand_%s_reg[%d]} $bank $b]
                    }
                }
            }
            set result [lsearch -all -inline -glob $cells $pattern]
            if {$::fault eq "missing_one_bit"} {set result [lrange $result 1 end]}
            if {$::fault eq "wrong_bit"} {lset result 0 {u_ctrl/cfg_r_reg[99][0]}}
            return $result
        }
        proc get_pins {args} {
            set at [lsearch -exact $args -of_objects]
            if {$at < 0} {return [list [lindex $args end]]}
            set pin [lindex [lindex $args end] end]
            set result {}
            foreach cell [lindex $args [expr {$at+1}]] {lappend result $cell/$pin}
            if {$::fault eq "missing_pin"} {return [lrange $result 1 end]}
            return $result
        }
        proc get_timing_paths {args} {
            if {$::fault eq "missing_paths"} { return {} }
            if {$::fault eq "missing_hold" && [lsearch -exact $args -hold] >= 0} { return {} }
            return [expr {[lsearch -exact $args -hold] >= 0 ? "hold_path" : "setup_path"}]
        }
        proc get_property {name path} {
            if {$name eq "SLACK"} {
                if {$::final && $::fault eq "final_setup" && $path eq "setup_path"} { return -0.1 }
                if {$::final && $::fault eq "final_hold" && $path eq "hold_path"} { return -0.1 }
                return 0.5
            }
            switch $name {
                PERIOD {return $::period}
                STARTPOINT_PIN { return a/Q }
                ENDPOINT_PIN { return b/D }
                LOGIC_LEVELS { return 2 }
                DATAPATH_DELAY { return 4 }
                REQUIREMENT { return 10 }
                default { error "unexpected property: $name" }
            }
        }
        foreach cmd {report_timing_summary report_timing report_clock_interaction check_timing report_drc report_utilization} {
            proc $cmd {args} {
                set idx [lsearch -exact $args -file]
                if {$idx >= 0} {
                    set fh [open [lindex $args [expr {$idx + 1}]] w]
                    puts $fh "synthetic test report"
                    close $fh
                }
            }
        }
    }
    set status [catch {interp eval $child [list source [file join $::script_root $script]]} message]
    set bit [interp eval $child {set bit_written}]
    interp delete $child
    return [list $status $message $bit]
}

try {
    foreach script {fmax.tcl build.tcl timing.tcl} {
        foreach fault {missing_paths missing_hold missing_cells missing_port missing_one_bit wrong_bit missing_pin final_setup final_hold structural} {
            lassign [run_flow $script $fault] status message bit
            check {$status && $message ne "EXIT:0" && !$bit} "$script rejects $fault ($message)"
            check {![file exists $tmp/fmax_validated_history.csv]} "failure publishes no successful frequency row"
        }
    }
    lassign [run_flow timing.tcl block_enabled] status message bit
    check {$status && $message eq "EXIT:0"} "block operand endpoints resolve at all 32 bits ($message)"
    lassign [run_flow timing.tcl missing_block_operand] status message bit
    check {$status && [string match {*block operands register coverage mismatch*} $message]} "one missing block operand bit fails coverage"
    lassign [run_flow fmax.tcl none] status message bit
    check {$status && $message eq "EXIT:0"} "passing Fmax run completes ($message)"
    set fh [open $tmp/fmax_validated_history.csv r]
    set csv [split [string trim [read $fh]] \n]
    close $fh
    set values [split [lindex $csv 1] ,]
    check {[llength $csv] == 2 && abs([lindex $values 5] - 1000.0/[lindex $values 6]) < 1e-6} "successful CSV uses validated period"
    lassign [run_flow build.tcl none] status message bit
    check {!$status && $bit} "passing build reaches bitstream generation"
    lassign [run_flow timing.tcl none] status message bit
    check {$status && $message eq "EXIT:0"} "passing STA gate completes"
} finally {
    file delete -force $tmp
}
puts "# $checks timing script checks passed (synthetic paths; Vivado still required)"
