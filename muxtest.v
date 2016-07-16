module muxtest;
    reg a;
    reg b;
    reg sel;
    wire o;

    assign o = sel ? b : a;

    initial begin
        $from_myhdl(a, b, sel);
        $to_myhdl(o);
    end
endmodule
