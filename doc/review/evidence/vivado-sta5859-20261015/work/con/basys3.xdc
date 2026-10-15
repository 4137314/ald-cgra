## basys3.xdc -- Digilent Basys 3 (Artix-7 xc7a35tcpg236-1)
## PHYSICAL constraints only (pins, I/O standards, config). All timing lives in
## scr/constraints.tcl (clock period, false paths, datapath multicycle) so the
## Fmax sweep can drive the period from one place.

## 100 MHz system clock (oscillator on pin W5)
set_property PACKAGE_PIN W5 [get_ports clk]
set_property IOSTANDARD LVCMOS33 [get_ports clk]

## Reset: centre push button (active high)
set_property PACKAGE_PIN U18 [get_ports btn_rst]
set_property IOSTANDARD LVCMOS33 [get_ports btn_rst]

## USB-UART bridge
set_property PACKAGE_PIN B18 [get_ports uart_rx_i]   ;# RsRx: host -> FPGA
set_property IOSTANDARD LVCMOS33 [get_ports uart_rx_i]
set_property PACKAGE_PIN A18 [get_ports uart_tx_o]   ;# RsTx: FPGA -> host
set_property IOSTANDARD LVCMOS33 [get_ports uart_tx_o]

## Status LEDs
set_property PACKAGE_PIN U16 [get_ports {led[0]}]
set_property PACKAGE_PIN E19 [get_ports {led[1]}]
set_property PACKAGE_PIN U19 [get_ports {led[2]}]
set_property PACKAGE_PIN V19 [get_ports {led[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[*]}]

## Configuration
set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
