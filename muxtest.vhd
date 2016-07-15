-- Very simple mux.
library ieee;
use ieee.std_logic_1164.all;

entity muxtest is
end entity;

architecture behavior of muxtest is
    signal to_myhdl_o : std_logic;
    signal from_myhdl_a : std_logic;
    signal from_myhdl_b : std_logic;
    signal from_myhdl_sel : std_logic;
begin
    to_myhdl_o <= from_myhdl_b when from_myhdl_sel='1' else from_myhdl_a;
end architecture;
