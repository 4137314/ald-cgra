# Execute on an isolated copy of the unmodified production RTL/constraints.
read_vhdl -vhdl2008 [glob rtl/*.vhd]
read_xdc con/nexys_a7.xdc
synth_design -top cgra_top -part xc7a100tcsg324-1 -flatten_hierarchy none -generic {G_STEP_DIV=2 G_ROWS=4 G_COLS=4}
source scr/constraints.tcl
cgra_timing_constraints 10.000 2
file mkdir reports
set fd [open reports/registers.txt w]
puts $fd "Vivado [version -short]; 4x4 STEP_DIV=2; synthesis only"
foreach pattern {*u_pe/r_reg* *cfg_r_reg* *west_r_reg* *north_r_reg* *rx_s1_reg *rx_s2_reg *rst_s1_reg *rst_s2_reg} {
    set cells [get_cells -hier -filter "REF_NAME =~ FD* && NAME =~ $pattern"]
    puts $fd "$pattern\t[llength $cells]"
    foreach c $cells {
        puts $fd "$c\t[get_property REF_NAME $c]\tASYNC_REG=[get_property ASYNC_REG $c]"
    }
}
close $fd
report_utilization -file reports/utilization.rpt
check_timing -verbose -file reports/check_timing.rpt
report_cdc -details -file reports/cdc.rpt
report_exceptions -coverage -file reports/exceptions.rpt
write_checkpoint reports/post_synth.dcp
set errors [get_msg_config -severity {ERROR} -count]
set critical [get_msg_config -severity {CRITICAL WARNING} -count]
puts "BASELINE_SYNTH errors=$errors critical=$critical"
if {$errors > 0 || $critical > 0} {exit 1}
exit 0
