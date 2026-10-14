module wrong_cross_set_type;
  bit a;
  bit b;
  typedef struct { bit only; } WrongVal;
  WrongVal wrong[$];
  covergroup cg;
    ca: coverpoint a;
    cb: coverpoint b;
    x: cross ca, cb {
      bins selected = wrong;
    }
  endgroup
endmodule

