// XFAIL: *
// RUN: %obelisk --std=1800-2023 -emit-obelisk %s -o %t.mlir

// IEEE 1800-2023 18.5.4 (Syntax 18-4, footnote 13) admits any singular or
// array variable in a uniqueness constraint, so a state array constrains the
// random leaves against its current values. Pristine Slang v11.0 reports
// "uniqueness constraints can only contain references to random variables".
class C;
  rand bit [3:0] values[2];
  bit [3:0] taken[2] = '{1, 2};
  constraint distinct { unique {values, taken}; }
endclass

module top;
  C c = new;
  initial void'(c.randomize());
endmodule
