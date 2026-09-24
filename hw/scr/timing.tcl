# timing.tcl - static timing analysis gate for the CGRA on a Xilinx target.
#
# Usage (from hw/):
#   vivado -mode batch -nolog -nojournal -source scr/timing.tcl -tclargs [board] [period_ns] [step_div] [rows] [cols]
# or simply:  make sta BOARD=nexys_a7
#
# Runs synth + implementation at the given clock period (default 10 ns = 100 MHz)
# then EXITS NON-ZERO unless both setup (WNS) and hold (WHS) slack are >= 0. A
# zero exit means the design is timing-clean at that clock, so a bitstream from
# the same sources is guaranteed to meet timing on the board. Set FLOORPLAN=1 to
# apply scr/floorplan.tcl. To search the maximum frequency use scr/fmax.tcl.
#
# Reports land in build/vivado/*_${board}.rpt.

set board  "nexys_a7"
if { $argc > 0 } { set board    [lindex $argv 0] }
set period 10.000
if { $argc > 1 } { set period   [lindex $argv 1] }
set step_div 2
if { $argc > 2 } { set step_div [lindex $argv 2] }

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
lassign [cgra_geometry $argv 3] mesh_rows mesh_cols outdir
file mkdir $outdir
puts "mesh: ${mesh_rows}x${mesh_cols}; outputs: $outdir"

# ---------------------------------------------------------------- build
read_vhdl -vhdl2008 [glob rtl/*.vhd]
read_xdc  con/${board}.xdc
source    scr/constraints.tcl
synth_design -top $top -part $part -generic [list G_STEP_DIV=$step_div G_ROWS=$mesh_rows G_COLS=$mesh_cols]
cgra_timing_constraints $period $step_div
if { [info exists ::env(FLOORPLAN)] && $::env(FLOORPLAN) ne "0" } {
    source scr/floorplan.tcl
    cgra_floorplan $board
}
opt_design
place_design
phys_opt_design
route_design

# ---------------------------------------------------------------- reports
report_timing_summary -file $outdir/timing_summary_${board}.rpt
report_timing -setup -max_paths 10 -nworst 1 -file $outdir/timing_setup_${board}.rpt
report_timing -hold  -max_paths 10 -nworst 1 -file $outdir/timing_hold_${board}.rpt
report_clock_interaction     -file $outdir/clock_interaction_${board}.rpt
check_timing -verbose        -file $outdir/check_timing_${board}.rpt

# ---------------------------------------------------------------- gate
lassign [cgra_timing_slacks] wns whs

puts "======================================================================"
puts "  STA gate: $top on $board ($part) @ ${period} ns"
puts "    setup  WNS = $wns ns"
puts "    hold   WHS = $whs ns"

set ok 1
if { $wns < 0 } { puts "    FAIL: setup timing violated"; set ok 0 }
if { $whs < 0 } { puts "    FAIL: hold timing violated";  set ok 0 }

if { $ok } {
    puts "    PASS: setup and hold met under the applied constraints."
    puts "======================================================================"
    exit 0
} else {
    puts "    see $outdir/timing_summary_${board}.rpt"
    puts "======================================================================"
    exit 1
}
