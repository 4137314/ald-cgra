# fmax.tcl — find the maximum clock frequency the CGRA closes timing at, and
# report WHERE the wall is (the limiting path) so each iteration knows what to
# attack next.
#
# Usage (from hw/):
#   vivado -mode batch -nolog -nojournal -source scr/fmax.tcl -tclargs [board] [lo_ns] [hi_ns] [iters] [step_div] [rows] [cols]
# or simply:  make fmax BOARD=nexys_a7
#
# Method: synthesise once, then binary-search the clock PERIOD in [lo,hi] ns.
# For each candidate period the post-synth checkpoint is re-opened fresh, the
# timing constraints (scr/constraints.tcl) are re-applied at that period, and a
# route is run; the period "passes" iff setup WNS >= 0 and hold WHS >= 0. The
# smallest passing candidate is rechecked with phys_opt in a final fresh run.
# Report 1000/period only if that final run meets setup AND hold. Placement is
# heuristic, so this search does not prove a global maximum. Changing the
# constraint does not change the physical board oscillator.
# Set FLOORPLAN=1 to fold in scr/floorplan.tcl.
#
# Outputs: build/vivado/fmax_${board}.rpt (limiting-path analysis) and one
# appended row in build/vivado/fmax_validated_history.csv (the improvement trail).

set board "nexys_a7"
if { $argc > 0 } { set board [lindex $argv 0] }
set lo 5.0
if { $argc > 1 } { set lo [lindex $argv 1] }
set hi 10.0
if { $argc > 2 } { set hi [lindex $argv 2] }
set iters 6
if { $argc > 3 } { set iters [lindex $argv 3] }
set step_div 2
if { $argc > 4 } { set step_div [lindex $argv 4] }

source scr/timing_checks.tcl
cgra_timing_args $lo $step_div
cgra_positive_real $hi hi_ns
if {$hi <= $lo} { error "hi_ns must exceed lo_ns" }
if {![string is integer -strict $iters] || $iters < 1} {
    error "iters must be a positive integer"
}

array set board_parts {
    basys3   xc7a35tcpg236-1
    nexys_a7 xc7a100tcsg324-1
}
if { ![info exists board_parts($board)] } {
    puts "ERROR: unknown board '$board'. Known: [array names board_parts]"
    exit 1
}
set part   $board_parts($board)
set top    cgra_top
source scr/geometry.tcl
lassign [cgra_geometry $argv 5] mesh_rows mesh_cols outdir
file mkdir $outdir
puts "mesh: ${mesh_rows}x${mesh_cols}; outputs: $outdir"
set use_fp [expr {[info exists ::env(FLOORPLAN)] && $::env(FLOORPLAN) ne "0"}]

# -------------------------------------------------- one place+route at a period
# Returns "wns whs" for the routed design constrained at $period ns.
proc try_period {period synth_dcp use_fp board step_div} {
    open_checkpoint $synth_dcp
    cgra_timing_constraints $period $step_div
    if { $use_fp } { cgra_floorplan $board }
    opt_design -quiet
    place_design -quiet
    route_design -quiet
    return [cgra_timing_slacks]
}

# -------------------------------------------------- synthesise once
read_vhdl -vhdl2008 [glob rtl/*.vhd]
read_xdc  con/${board}.xdc
source    scr/constraints.tcl
if { $use_fp } { source scr/floorplan.tcl }
synth_design -top $top -part $part -generic [list G_STEP_DIV=$step_div G_ROWS=$mesh_rows G_COLS=$mesh_cols]
set synth_dcp $outdir/post_synth_fmax_${board}.dcp
write_checkpoint -force $synth_dcp

# -------------------------------------------------- binary search
# Establish an actually passing upper endpoint before narrowing the interval.
lassign [try_period $hi $synth_dcp $use_fp $board $step_div] wns whs
cgra_checked_frequency $hi $wns $whs
set best_period $hi
puts "=========== Fmax search: $top on $board ($part), step_div=$step_div ==========="
puts [format "  %-8s %-10s %-10s %s" period(ns) WNS(ns) WHS(ns) result]
for {set i 0} {$i < $iters} {incr i} {
    set mid [expr {($lo + $hi) / 2.0}]
    lassign [try_period $mid $synth_dcp $use_fp $board $step_div] wns whs
    set pass [expr {$wns >= 0 && $whs >= 0}]
    puts [format "  %-8.3f %-10.3f %-10.3f %s" $mid $wns $whs [expr {$pass ? "PASS" : "fail"}]]
    if { $pass } { set best_period $mid; set hi $mid } else { set lo $mid }
}

# -------------------------------------------------- final clean run @ best
# Re-run at the best period WITH phys_opt + full reports, and analyse the wall.
open_checkpoint $synth_dcp
cgra_timing_constraints $best_period $step_div
if { $use_fp } { cgra_floorplan $board }
opt_design -quiet
place_design -quiet
phys_opt_design -quiet
route_design -quiet

report_timing_summary -file $outdir/fmax_summary_${board}.rpt
report_timing -setup -max_paths 8 -nworst 1 -input_pins -file $outdir/fmax_${board}.rpt
report_timing -hold -max_paths 8 -nworst 1 -file $outdir/fmax_hold_${board}.rpt
report_utilization -file $outdir/utilization_fmax_${board}.rpt

# A passing search candidate does not imply that this different final run
# passes. Error out before printing a result or appending a successful row.
lassign [cgra_timing_slacks] wns whs
set fmax [cgra_checked_frequency $best_period $wns $whs]
set sp [cgra_nonempty [get_timing_paths -max_paths 1 -nworst 1 -setup] "final setup path"]
write_checkpoint -force $outdir/fmax_validated_${board}.dcp

# limiting-path facts
set src  [get_property STARTPOINT_PIN $sp]
set dst  [get_property ENDPOINT_PIN   $sp]
set lvls [get_property LOGIC_LEVELS   $sp]
set dpath [get_property DATAPATH_DELAY $sp]
# is the wall a real single-cycle path or the 2-cycle datapath?
set req  [get_property REQUIREMENT $sp]
set kind [expr {$req > ($best_period * 1.5) ? "datapath (multicycle)" : "single-cycle (control/reset/IO)"}]

# utilisation headlines
proc util_pct {rpt pat} {
    set fh [open $rpt r]; set d [read $fh]; close $fh
    foreach ln [split $d \n] { if {[regexp $pat $ln -> u]} { return $u } }
    return "?"
}
set lut [util_pct $outdir/utilization_fmax_${board}.rpt {CLB LUTs\*?\s+\|\s+(\d+)}]
if {$lut eq "?"} { set lut [util_pct $outdir/utilization_fmax_${board}.rpt {Slice LUTs\*?\s+\|\s+(\d+)}] }
set dsp [util_pct $outdir/utilization_fmax_${board}.rpt {DSPs\s+\|\s+(\d+)}]

puts "======================================================================"
puts "  Fmax RESULT: $top on $board ($part)"
puts [format "    best closing period = %.3f ns" $best_period]
puts [format "    setup slack (WNS)   = %.3f ns   hold (WHS) = %.3f ns" $wns $whs]
puts [format "    validated frequency = %.3f MHz  (1000 / constrained period)" $fmax]
puts "    ---- the wall (optimise this next) ----"
puts "    limiting path kind  = $kind"
puts "    startpoint          = $src"
puts "    endpoint            = $dst"
puts [format "    logic levels        = %s   datapath delay = %.3f ns" $lvls $dpath]
puts "    LUTs used = $lut   DSPs used = $dsp"
puts "    full path -> $outdir/fmax_${board}.rpt"
puts "======================================================================"

# -------------------------------------------------- append the improvement trail
set csv $outdir/fmax_validated_history.csv
set new [expr {![file exists $csv]}]
set fh [open $csv a]
if { $new } { puts $fh "utc,board,rows,cols,step_div,validated_mhz,period_ns,wns_ns,whs_ns,kind,logic_levels,startpoint,endpoint" }
puts $fh [format "%s,%s,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%s,%s,%s,%s" \
          [clock format [clock seconds] -format %Y-%m-%dT%H:%M:%SZ -gmt 1] \
          $board $mesh_rows $mesh_cols $step_div $fmax $best_period $wns $whs "\"$kind\"" $lvls "\"$src\"" "\"$dst\""]
close $fh
puts "  logged to $csv"
exit 0
