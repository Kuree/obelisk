// RUN: obelisk -O0 %s -o %t.native
// RUN: not %t.native
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: not %t.bytecode

module native_covergroup_null;
  covergroup cg;
    cp: coverpoint 1 {
      bins one = {1};
    }
  endgroup
  cg c;
  initial c.sample();
endmodule
