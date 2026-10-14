// XFAIL: *
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s -o %t.mlir

// IEEE 19.8.1 permits a defaulted sample formal, and 13.5.3 evaluates a
// default in the scope containing the declaration, here the class. Pristine
// Slang v11.0 reports `value` as undeclared in the defaulted sample formal.
module top;
  class C;
    int value;
    covergroup cg with function sample(int extra = value);
      cp: coverpoint value + extra { bins one = {1}; }
    endgroup
    function new();
      cg = new;
    endfunction
    function void sample_it();
      cg.sample();
    endfunction
  endclass
  C c;
  initial begin
    c = new;
    c.sample_it();
  end
endmodule
