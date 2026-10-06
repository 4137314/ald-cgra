# program.tcl — program the FPGA over JTAG with the Vivado hardware server.
#
# Usage (from hw/):
#   vivado -mode batch -nolog -nojournal -source scr/program.tcl -tclargs <bitfile>
# or simply:  make prog
#
# Alternative without Vivado:  openFPGALoader -b basys3 <bitfile>  (make prog-ofl)

if { $argc < 1 } {
    puts "ERROR: usage: program.tcl <bitfile>"
    exit 1
}
set bitfile [lindex $argv 0]

if { ![file exists $bitfile] } {
    puts "ERROR: bitstream '$bitfile' not found — run 'make bit' first"
    exit 1
}

open_hw_manager
connect_hw_server
open_hw_target

set dev [lindex [get_hw_devices] 0]
current_hw_device $dev
refresh_hw_device $dev

set_property PROGRAM.FILE $bitfile $dev
program_hw_devices $dev

puts "OK: programmed [get_property PART $dev] with $bitfile"
close_hw_manager
