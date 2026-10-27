// XFAIL: *
// RUN: %talos -emit-slang %s -o - | FileCheck %s

// IEEE 1800-2023 23.10.1: with multiple defparams for a single parameter, the
// parameter takes the value of the last one in the source text. Slang v11
// keeps the first override and warns that it ignores the later one.
module Leaf;
  parameter int P = 0;
endmodule
module top;
  Leaf u();
  defparam u.P = 1;
  defparam u.P = 2;
endmodule

// CHECK: slang.symbol.parameter @{{.*}}.P attributes {constant_value = "2"
