# build.tcl — Vivado non-project batch flow: sources -> bitstream.
#
# Usage (from hw/):
#   vivado -mode batch -nolog -nojournal -source scr/build.tcl -tclargs [board]
# or simply:  make bit BOARD=basys3
#
# Outputs in build/vivado/: checkpoints, reports and <top>_<board>.bit

set board "basys3"
if { $argc > 0 } { set board [lindex $argv 0] }

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
set outdir build/vivado

file mkdir $outdir

# ---------------------------------------------------------------- sources
read_vhdl -vhdl2008 [glob rtl/*.vhd]
read_xdc  con/${board}.xdc

# ---------------------------------------------------------------- synthesis
synth_design -top $top -part $part
write_checkpoint  -force $outdir/post_synth.dcp
report_utilization -file $outdir/utilization_synth.rpt

# ---------------------------------------------------------------- implementation
opt_design
place_design
phys_opt_design
route_design
write_checkpoint      -force $outdir/post_route.dcp
report_timing_summary -file  $outdir/timing_summary.rpt
report_utilization    -file  $outdir/utilization_impl.rpt
report_drc            -file  $outdir/drc.rpt

# Fail loudly if timing is not met.
set wns [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -setup]]
if { $wns < 0 } {
    puts "WARNING: negative setup slack (WNS = $wns ns) — check $outdir/timing_summary.rpt"
}

# ---------------------------------------------------------------- bitstream
write_bitstream -force $outdir/${top}_${board}.bit
puts "OK: bitstream written to $outdir/${top}_${board}.bit"
