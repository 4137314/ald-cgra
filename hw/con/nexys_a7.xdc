## nexys_a7.xdc — Digilent Nexys A7-100T / Nexys 4 DDR (Artix-7 xc7a100tcsg324-1)

## 100 MHz system clock
set_property PACKAGE_PIN E3 [get_ports clk]
set_property IOSTANDARD LVCMOS33 [get_ports clk]
create_clock -period 10.000 -name sys_clk [get_ports clk]

## Reset: center push button (active high)
set_property PACKAGE_PIN N17 [get_ports btn_rst]
set_property IOSTANDARD LVCMOS33 [get_ports btn_rst]

## USB-UART bridge
set_property PACKAGE_PIN C4 [get_ports uart_rx_i]    ;# UART_TXD_IN: host -> FPGA
set_property IOSTANDARD LVCMOS33 [get_ports uart_rx_i]
set_property PACKAGE_PIN D4 [get_ports uart_tx_o]    ;# UART_RXD_OUT: FPGA -> host
set_property IOSTANDARD LVCMOS33 [get_ports uart_tx_o]

## Status LEDs
set_property PACKAGE_PIN H17 [get_ports {led[0]}]
set_property PACKAGE_PIN K15 [get_ports {led[1]}]
set_property PACKAGE_PIN J13 [get_ports {led[2]}]
set_property PACKAGE_PIN N14 [get_ports {led[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {led[*]}]

## Asynchronous I/O: exclude from timing analysis
set_false_path -from [get_ports uart_rx_i]
set_false_path -to   [get_ports uart_tx_o]
set_false_path -from [get_ports btn_rst]
set_false_path -to   [get_ports {led[*]}]

## Configuration
set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
