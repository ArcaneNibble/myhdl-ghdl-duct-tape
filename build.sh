#!/bin/bash

set -xeuo pipefail

ghdl -a clk.vhd
ghdl -e clk

# Um, yeah... about this...
iverilog-vpi myhdl.c

MYHDL_TO_PIPE=0 MYHDL_FROM_PIPE=1 ./clk --stop-time=200ns --vcd=clk.vcd --vpi=./myhdl.vpi
