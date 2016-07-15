-- Very simple clock. Cannot actually be used with MyHDL because it contains
-- delays. Used to test basic structure.
library ieee;
use ieee.std_logic_1164.all;

entity clk is
end entity;

architecture behavior of clk is
    signal to_myhdl_o : std_logic := '0';
begin
    to_myhdl_o <= not to_myhdl_o after 10 ns;
end architecture;
