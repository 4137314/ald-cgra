# program.tcl — program the FPGA over JTAG with the Vivado hardware server.
#
# Usage (from hw/):
# Internal helper: artifacts.py validates the explicit manifest and file hashes
# before invoking this script with <bitfile> <expected-part>.
# User entry point: make prog ARTIFACT=/absolute/path/to/manifest.json
#
# Alternative without Vivado:  openFPGALoader -b basys3 <bitfile>  (make prog-ofl)

if { $argc != 2 } {
    puts "ERROR: use make prog ARTIFACT=... (bitfile and expected part required)"
    exit 1
}
set bitfile [lindex $argv 0]
set expected_part [lindex $argv 1]
array set jtag_parts {xc7a100tcsg324-1 xc7a100t xc7a35tcpg236-1 xc7a35t}
if {![info exists jtag_parts($expected_part)]} {
    puts "ERROR: unsupported expected part $expected_part"
    exit 1
}

if { ![file exists $bitfile] } {
    puts "ERROR: bitstream '$bitfile' not found — run 'make bit' first"
    exit 1
}

open_hw_manager
connect_hw_server
open_hw_target

# JTAG identifies the die, not the board/package/speed grade. Those remain the
# explicit caller's board selection, checked against the artifact manifest.
set devices [get_hw_devices -filter "PART == $jtag_parts($expected_part)"]
if {[llength $devices] != 1} {
    puts "ERROR: expected exactly one device of part $expected_part; found $devices"
    close_hw_manager
    exit 1
}
set dev [lindex $devices 0]
current_hw_device $dev
refresh_hw_device $dev

set_property PROGRAM.FILE $bitfile $dev
program_hw_devices $dev

puts "OK: programmed [get_property PART $dev] with $bitfile"
close_hw_manager
