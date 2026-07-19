# synth.tcl — Vivado synthesis-only check for the CGRA.
#
# Usage (from hw/):
#   vivado -mode batch -nolog -nojournal -source scr/synth.tcl -tclargs [board] [period_ns]
# or simply:  make synth BOARD=nexys_a7
#
# Runs synthesis ONLY (no place/route), applies the timing constraints, writes
# the post-synth checkpoint and utilization/timing reports, then EXITS NON-ZERO
# if synthesis produced any ERROR or CRITICAL WARNING. The fast RTL gate: it
# catches syntax, elaboration, inference and constraint problems in ~seconds
# without paying for a full implementation. Use `make sta`/`make fmax` for
# timing closure and Fmax.
#
# Reports land in build/vivado/*_synth_${board}.rpt.

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
set outdir build/vivado
file mkdir $outdir

# ---------------------------------------------------------------- sources
read_vhdl -vhdl2008 [glob rtl/*.vhd]
read_xdc  con/${board}.xdc
source    scr/constraints.tcl

# ---------------------------------------------------------------- synthesis
# -flatten_hierarchy none keeps the RTL hierarchy in the reports so a warning
# points at the module it came from.
if { [catch { synth_design -top $top -part $part -flatten_hierarchy none \
                           -generic G_STEP_DIV=$step_div } err] } {
    puts "ERROR: synth_design failed: $err"
    exit 1
}

# Timing constraints resolve against the synthesised netlist (cell-based
# multicycle exception needs real leaf registers). step_div matches the generic.
cgra_timing_constraints $period $step_div

write_checkpoint   -force $outdir/post_synth_${board}.dcp
report_utilization -file  $outdir/utilization_synth_${board}.rpt
report_timing_summary -max_paths 10 -file $outdir/timing_synth_${board}.rpt

# ---------------------------------------------------------------- gate
set n_err  [get_msg_config -severity {ERROR}            -count]
set n_crit [get_msg_config -severity {CRITICAL WARNING} -count]

puts "======================================================================"
puts "  synth gate: $top on $board ($part) @ ${period} ns"
puts "    errors            = $n_err"
puts "    critical warnings = $n_crit"

if { $n_err > 0 || $n_crit > 0 } {
    puts "    FAIL: synthesis reported errors / critical warnings above."
    puts "    see $outdir/utilization_synth_${board}.rpt"
    puts "======================================================================"
    exit 1
}

puts "    PASS: RTL synthesised clean for $part."
puts "======================================================================"
exit 0
