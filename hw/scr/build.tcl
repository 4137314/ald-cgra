# build.tcl — Vivado non-project batch flow: sources -> bitstream.
#
# Usage (from hw/):
#   vivado -mode batch -nolog -nojournal -source scr/build.tcl -tclargs [board] [period_ns] [step_div] [rows] [cols]
# or simply:  make bit BOARD=nexys_a7
#
# Set FLOORPLAN=1 in the environment to apply scr/floorplan.tcl before place.
# Outputs in build/vivado/: checkpoints, reports and <top>_<board>.bit

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
    puts "ERROR: unknown board '$board'. Known boards: [array names board_parts]"
    exit 1
}

set part   $board_parts($board)
set top    cgra_top
source scr/geometry.tcl
lassign [cgra_geometry $argv 3] mesh_rows mesh_cols outdir
file mkdir $outdir
puts "mesh: ${mesh_rows}x${mesh_cols}; outputs: $outdir"

# ---------------------------------------------------------------- sources
read_vhdl -vhdl2008 [glob rtl/*.vhd]
read_xdc  con/${board}.xdc
source    scr/constraints.tcl

# ---------------------------------------------------------------- synthesis
synth_design -top $top -part $part -generic [list G_STEP_DIV=$step_div G_ROWS=$mesh_rows G_COLS=$mesh_cols]
cgra_timing_constraints $period $step_div
write_checkpoint  -force $outdir/post_synth.dcp
report_utilization -file $outdir/utilization_synth.rpt

# ---------------------------------------------------------------- floorplan
if { [info exists ::env(FLOORPLAN)] && $::env(FLOORPLAN) ne "0" } {
    source scr/floorplan.tcl
    cgra_floorplan $board
}

# ---------------------------------------------------------------- implementation
opt_design
place_design
phys_opt_design
route_design
write_checkpoint      -force $outdir/post_route.dcp
report_timing_summary -file  $outdir/timing_summary.rpt
report_utilization    -file  $outdir/utilization_impl.rpt
report_drc            -file  $outdir/drc.rpt

# ---------------------------------------------------------------- timing gate
# Require nonempty setup/hold paths with nonnegative slack for this build.
# This gate depends on correct constraints; it does not verify a physical board.
# Reports/bitstreams left by an older run are not evidence for this run.
lassign [cgra_timing_slacks] wns whs
puts "timing: setup WNS = $wns ns, hold WHS = $whs ns"
if { $wns < 0 || $whs < 0 } {
    puts "ERROR: timing NOT met (WNS=$wns ns, WHS=$whs ns) — no bitstream written."
    puts "       inspect $outdir/timing_summary.rpt"
    exit 1
}

# ---------------------------------------------------------------- bitstream
write_bitstream -force $outdir/${top}_${board}.bit
puts "OK: timing met; bitstream written to $outdir/${top}_${board}.bit"
