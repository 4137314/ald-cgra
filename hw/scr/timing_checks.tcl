# Shared timing gates. Pure numeric checks are also exercised without Vivado.
proc cgra_positive_real {value name} {
    if {![string is double -strict $value] ||
        [catch {expr {$value > 0 && $value < Inf}} valid] || !$valid} {
        error "$name must be a finite positive number (got '$value')"
    }
}

proc cgra_timing_args {period step_div} {
    cgra_positive_real $period period
    if {![string is integer -strict $step_div] || $step_div < 1} {
        error "step_div must be a positive integer"
    }
}

proc cgra_nonempty {objects name} {
    if {[llength $objects] == 0} { error "no objects matched $name" }
    return $objects
}

proc cgra_finite_slack {value name} {
    if {![string is double -strict $value] ||
        [catch {expr {$value > -Inf && $value < Inf}} valid] || !$valid} {
        error "$name is not a finite slack value (got '$value')"
    }
}

proc cgra_timing_slacks {} {
    set sp [cgra_nonempty [get_timing_paths -max_paths 1 -nworst 1 -setup] "setup paths"]
    set hp [cgra_nonempty [get_timing_paths -max_paths 1 -nworst 1 -hold] "hold paths"]
    set wns [get_property SLACK $sp]
    set whs [get_property SLACK $hp]
    cgra_finite_slack $wns WNS
    cgra_finite_slack $whs WHS
    return [list $wns $whs]
}

# Report only the frequency of the period actually constrained and checked.
# WNS cannot be subtracted from the clock period for arbitrary multicycle paths.
proc cgra_checked_frequency {period wns whs} {
    cgra_positive_real $period period
    cgra_finite_slack $wns WNS
    cgra_finite_slack $whs WHS
    if {$wns < 0 || $whs < 0} {
        error "timing NOT met: WNS=$wns ns, WHS=$whs ns"
    }
    return [expr {1000.0 / $period}]
}
