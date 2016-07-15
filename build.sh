#!/bin/bash

set -xeuo pipefail

ghdl -a muxtest.vhd
ghdl -e muxtest

# Um, yeah... about this...
iverilog-vpi myhdl.c

MYHDL_TO_PIPE=0 MYHDL_FROM_PIPE=1 ./muxtest --stop-time=200ns --vcd=muxtest.vcd --vpi=./myhdl.vpi
