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
        set final 0
        set bit_written 0
        set checkpoints {}
        rename source real_source
        proc source {args} {
            set path [lindex $args end]
            if {[file tail $path] eq "geometry.tcl"} {
                proc cgra_geometry {args} { return [list 4 4 $::outdir] }
                return
            }
            uplevel 1 [list real_source {*}$args]
        }
        proc exit {code} { error "EXIT:$code" }
        foreach cmd {read_vhdl read_xdc synth_design open_checkpoint opt_design place_design route_design create_clock set_false_path set_multicycle_path} {
            proc $cmd {args} {}
        }
        proc phys_opt_design {args} { set ::final 1 }
        proc write_checkpoint {args} { lappend ::checkpoints [lindex $args end] }
        proc write_bitstream {args} { set ::bit_written 1 }
        proc get_ports {pattern} {
            if {$::fault eq "missing_port" && $pattern eq "clk"} { return {} }
            return [list $pattern]
        }
        proc get_cells {args} {
            if {$::fault eq "missing_cells"} { return {} }
            return [list register]
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
        foreach fault {missing_paths missing_hold missing_cells missing_port final_setup final_hold} {
            lassign [run_flow $script $fault] status message bit
            check {$status && $message ne "EXIT:0" && !$bit} "$script rejects $fault ($message)"
            check {![file exists $tmp/fmax_validated_history.csv]} "failure publishes no successful frequency row"
        }
    }
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
