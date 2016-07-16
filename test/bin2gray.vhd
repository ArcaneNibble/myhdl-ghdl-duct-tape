library ieee;
use ieee.std_logic_1164.all;

entity bin2gray is
    port(
        B   : in std_logic_vector(7 downto 0);
        G   : out std_logic_vector(7 downto 0));
end entity;

architecture behavior of bin2gray is
begin
    G(7) <= B(7);
    G(6 downto 0) <= B(6 downto 0) xor B(7 downto 1);
end architecture;
