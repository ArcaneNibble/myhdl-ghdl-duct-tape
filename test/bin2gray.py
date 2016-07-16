import os

from myhdl import Cosimulation

cmd_analysis = "ghdl -a bin2gray.vhd dut_bin2gray.vhd"
cmd_elab = "ghdl -e dut_bin2gray"
      
def bin2gray(B, G):
    os.system(cmd_analysis)
    os.system(cmd_elab)
    return Cosimulation("./dut_bin2gray --vpi=../myhdl.vpi", from_myhdl_b=B, to_myhdl_g=G)
               

    
