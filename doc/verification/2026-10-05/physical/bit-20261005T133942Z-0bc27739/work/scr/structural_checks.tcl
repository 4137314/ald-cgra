# Real-netlist checks for the two asynchronous input synchronizers. This is
# structural/placement evidence, not a metastability or board certification.
source [file join [file dirname [info script]] timing_checks.tcl]

proc cgra_check_synchronizers {report {placed 0}} {
    set fd [open $report w]
    try {
        puts $fd "stage\tASYNC_REG\tLOC\tBEL\tQ_loads"
        foreach pair {{u_rx/rx_s1_reg u_rx/rx_s2_reg} {rst_s1_reg rst_s2_reg}} {
            lassign $pair first second
            set locations {}
            foreach name $pair {
                set cell [get_cells -hier -filter "NAME == $name"]
                if {[llength $cell] != 1} {error "synchronizer stage missing or duplicated: $name"}
                set attr [get_property ASYNC_REG $cell]
                if {![string is boolean -strict $attr] || !$attr} {
                    error "ASYNC_REG not preserved: $name"
                }
                set q [get_pins -of_objects $cell -filter {REF_PIN_NAME == Q}]
                set nets [get_nets -segments -of_objects $q]
                set loads [lsort -unique [get_pins -leaf -of_objects $nets -filter {DIRECTION == IN}]]
                if {$name eq $first && $loads ne [list $second/D]} {
                    error "first synchronizer stage must drive only $second/D: $loads"
                }
                cgra_nonempty $loads "synchronizer loads of $name"
                set loc [get_property LOC $cell]
                lappend locations $loc
                puts $fd "$name\t$attr\t$loc\t[get_property BEL $cell]\t$loads"
            }
            if {$placed && ([lindex $locations 0] eq "" || [lindex $locations 0] ne [lindex $locations 1])} {
                error "synchronizer pair not placed in the same slice: $pair -> $locations"
            }
            set first_clk [get_clocks -of_objects [get_pins $first/C]]
            set second_clk [get_clocks -of_objects [get_pins $second/C]]
            if {[llength $first_clk] != 1 || $first_clk ne $second_clk} {
                error "synchronizer stages do not share one constrained clock: $pair"
            }
        }
    } finally {close $fd}
}

# Data D pins are multicycle; control CE/reset pins must stay single-cycle.
# Require a real setup path for every PE control pin in this RTL instance.
proc cgra_check_control_paths {period report} {
    set cells [cgra_nonempty [get_cells -hier -filter {REF_NAME =~ FD* && NAME =~ *u_pe/r_reg*}] {PE registers}]
    set pins [get_pins -of_objects $cells -filter {REF_PIN_NAME == CE || REF_PIN_NAME == R || REF_PIN_NAME == S}]
    set fd [open $report w]
    try {
        puts $fd "endpoint\tstartpoint\trequirement_ns\tslack_ns"
        foreach pin $pins {
            set paths [get_timing_paths -setup -max_paths 1 -nworst 1 -to $pin]
            cgra_nonempty $paths "single-cycle control path to $pin"
            set requirement [get_property REQUIREMENT $paths]
            if {abs($requirement - $period) > 0.000001} {
                error "control path relaxed: $pin requirement=$requirement, period=$period"
            }
            puts $fd "$pin\t[get_property STARTPOINT_PIN $paths]\t$requirement\t[get_property SLACK $paths]"
        }
        puts $fd "checked_control_endpoints\t[llength $pins]"
    } finally {close $fd}
}

proc cgra_check_data_paths {period step_div rows cols report} {
    lassign [cgra_datapath_registers $rows $cols] src dst
    set launch [get_pins -of_objects $src -filter {REF_PIN_NAME == C}]
    set pins [get_pins -of_objects $dst -filter {REF_PIN_NAME == D}]
    set fd [open $report w]
    try {
        puts $fd "endpoint\tsetup_requirement_ns\thold_requirement_ns"
        foreach pin $pins {
            set setup [cgra_nonempty [get_timing_paths -setup -from $launch -to $pin -max_paths 1] "datapath setup to $pin"]
            set hold [cgra_nonempty [get_timing_paths -hold -from $launch -to $pin -max_paths 1] "datapath hold to $pin"]
            set sr [get_property REQUIREMENT $setup]
            set hr [get_property REQUIREMENT $hold]
            if {abs($sr - $period*$step_div) > 0.000001 || abs($hr) > 0.000001} {
                error "datapath requirement mismatch: $pin setup=$sr hold=$hr"
            }
            puts $fd "$pin\t$sr\t$hr"
        }
        puts $fd "launch_registers\t[llength $src]"
        puts $fd "data_endpoints\t[llength $pins]"
    } finally {close $fd}
}

proc cgra_read_report {path} {
    set fd [open $path r]
    try {return [read $fd]} finally {close $fd}
}

# A PASS here qualifies only this physical model and these explicit exceptions.
# Keep all reports, including failures. A completed JSON is written last.
proc cgra_physical_gate {outdir period step_div rows cols placed} {
    cgra_check_synchronizers $outdir/synchronizers.tsv $placed
    cgra_check_control_paths $period $outdir/control_endpoints.tsv
    cgra_check_data_paths $period $step_div $rows $cols $outdir/data_endpoints.tsv
    report_exceptions -coverage -file $outdir/exception_coverage.rpt
    report_cdc -details -file $outdir/cdc.rpt
    set cdc [cgra_read_report $outdir/cdc.rpt]
    if {[regexp {CDC-[0-9]+[ \t]+(Critical|Warning|Error)} $cdc]} {error "CDC warnings: see cdc.rpt"}
    if {![regexp {CDC-3[ \t]+Info[ \t]+2[ \t]+} $cdc]} {
        error "expected two recognized synchronizer chains in CDC report"
    }
    check_timing -verbose -file $outdir/constraint_coverage.rpt
    set timing [cgra_read_report $outdir/constraint_coverage.rpt]
    foreach check {no_clock constant_clock pulse_width_clock unconstrained_internal_endpoints multiple_clock generated_clocks loops partial_input_delay partial_output_delay latch_loops} {
        if {![regexp "checking $check \\(0\\)" $timing]} {error "constraint coverage: $check failed or missing"}
    }
    # Explicit async board ports have false paths. Other ports must not become
    # silently unconstrained; the port set itself is checked by constraints.tcl.
    if {![regexp {There are 0 input ports with no input delay specified} $timing] ||
        ![regexp {There are 0 ports with no output delay specified} $timing]} {
        error "unexpected I/O without a delay or explicit exception"
    }
    report_drc -file $outdir/drc_gate.rpt
    set bad_drc [get_drc_violations -quiet -filter {SEVERITY == Error || SEVERITY == {Critical Warning}}]
    if {[llength $bad_drc]} {error "DRC errors/critical warnings: $bad_drc"}
    set errors [get_msg_config -severity ERROR -count]
    set critical [get_msg_config -severity {CRITICAL WARNING} -count]
    if {$errors || $critical} {error "Vivado emitted $errors errors and $critical critical warnings"}
    set gates {"synthesis":"PASS","synchronizers":"PASS","endpoint_coverage":"PASS","control_timing":"PASS","cdc":"PASS","constraints":"PASS","drc":"PASS"}
    set metrics "\"launch_registers\":[expr {42*$rows*$cols+16*($rows+$cols)}],\"data_endpoints\":[expr {16*$rows*$cols}]"
    if {$placed} {
        lassign [cgra_timing_slacks] wns whs
        cgra_checked_frequency $period $wns $whs
        append gates {,"setup":"PASS","hold":"PASS"}
        append metrics ",\"wns_ns\":$wns,\"whs_ns\":$whs"
    }
    set fd [open $outdir/gates.json w]
    puts $fd "{\"schema_version\":1,\"period_ns\":$period,\"gates\":{$gates},\"metrics\":{$metrics}}"
    close $fd
}
