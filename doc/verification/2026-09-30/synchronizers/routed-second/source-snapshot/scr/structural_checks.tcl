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
