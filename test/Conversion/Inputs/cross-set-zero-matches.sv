module zero_cross_set_matches;
  bit a;
  bit b;
  covergroup cg;
    ca: coverpoint a;
    cb: coverpoint b;
    x: cross ca, cb {
      bins selected = '{'{0, 1}} matches 0;
    }
  endgroup
endmodule
