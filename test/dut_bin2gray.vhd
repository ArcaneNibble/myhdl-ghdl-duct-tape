library ieee;
use ieee.std_logic_1164.all;

entity dut_bin2gray is
end entity;

architecture behavior of dut_bin2gray is
    signal to_myhdl_G : std_logic_vector(7 downto 0);
    signal from_myhdl_B : std_logic_vector(7 downto 0);

    component bin2gray
        port(
            B   : in std_logic_vector(7 downto 0);
            G   : out std_logic_vector(7 downto 0));
    end component;
begin
    dut : bin2gray port map (
        B => from_myhdl_B,
        G => to_myhdl_G
    );
end architecture;
