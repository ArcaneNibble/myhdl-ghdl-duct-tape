ghdl -a clk.vhd
ghdl -e clk
./clk --stop-time=200ns --vcd=clk.vcd
