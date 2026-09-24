# Shared geometry arguments for all Vivado entry points. The byte protocol
# retains its 16-bit EXEC mask; larger standalone arrays need a new protocol.
proc cgra_geometry {arguments first} {
    set rows 4
    set cols 4
    if {[llength $arguments] > $first} { set rows [lindex $arguments $first] }
    if {[llength $arguments] > $first + 1} { set cols [lindex $arguments [expr {$first + 1}]] }
    foreach value [list $rows $cols] {
        if {![regexp {^[1-9][0-9]*$} $value] || $value > 16} {
            error "mesh dimensions must be integers in 1..16"
        }
    }
    if {$rows * $cols > 16} { error "protocol v3 supports at most 16 PEs" }
    set outdir build/vivado
    if {$rows != 4 || $cols != 4} { append outdir /${rows}x${cols} }
    return [list $rows $cols $outdir]
}
