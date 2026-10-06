read_vhdl -vhdl2008 [glob rtl/*.vhd]
read_xdc con/nexys_a7.xdc
synth_design -top cgra_top -part xc7a100tcsg324-1 -generic {G_STEP_DIV=2 G_ROWS=4 G_COLS=4}
source scr/constraints.tcl
source scr/structural_checks.tcl
cgra_timing_constraints 10.000 2 4 4
file mkdir reports
cgra_check_synchronizers reports/sync_synth.tsv
cgra_check_control_paths 10.000 reports/control_synth.tsv
report_utilization -file reports/utilization_synth.rpt
report_exceptions -coverage -file reports/exceptions_synth.rpt
report_cdc -details -file reports/cdc_synth.rpt
write_checkpoint reports/post_synth.dcp
opt_design
place_design
phys_opt_design
route_design
cgra_check_synchronizers reports/sync_routed.tsv 1
cgra_check_control_paths 10.000 reports/control_routed.tsv
check_timing -verbose -file reports/check_timing.rpt
report_cdc -details -file reports/cdc_routed.rpt
report_exceptions -coverage -file reports/exceptions_routed.rpt
report_drc -file reports/drc.rpt
report_timing_summary -file reports/timing.rpt
report_utilization -file reports/utilization_routed.rpt
write_checkpoint reports/post_route.dcp
lassign [cgra_timing_slacks] wns whs
cgra_checked_frequency 10.000 $wns $whs
puts "ROUTED_CHECK_PASS WNS=$wns WHS=$whs"
exit 0
