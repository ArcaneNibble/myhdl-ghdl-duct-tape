#!/bin/bash

set -xeuo pipefail

ghdl -a clk.vhd
ghdl -e clk

# Um, yeah... about this...
iverilog-vpi myhdl.c

./clk --stop-time=200ns --vcd=clk.vcd --vpi=./myhdl.vpi
