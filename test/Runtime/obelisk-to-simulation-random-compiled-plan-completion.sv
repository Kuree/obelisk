// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t +ntb_random_seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5 and 18.5.13: live captures, sparse solutions, and soft
// priorities require proofs rather than bounded traversal. The nonlinear
// equation x * x == 49 modulo 2**32 has four roots; the highest-priority soft
// default x == 9 is impossible, so the next soft x == 7 selects the root.
// CHECK: class status 1
// CHECK-NEXT: class root 7
// CHECK-NEXT: scope status 1
// CHECK-NEXT: scope root 7

// x * x == 2 has no 32-bit root. A failed solve preserves the previous random
// value (18.6.3).
// CHECK-NEXT: unsat status 0
// CHECK-NEXT: unchanged root 7

// The wide system-function constraints have exactly one solution, far beyond
// the interpreter search window of a 128-bit domain.
// CHECK-NEXT: wide status 1
// CHECK-NEXT: wide matches 1

class C;
  rand bit [31:0] x;
  bit [31:0] limit = 49;
  bit [31:0] preferred = 9;
  constraint rules {
    x * x == limit;
    soft x == 7;
    soft x == preferred;
  }
endclass

module top;
  C object = new;
  bit [31:0] x;
  bit [31:0] limit = 49;
  bit [31:0] preferred = 9;
  bit [127:0] wide;
  int logarithm = 96;
  initial begin
    $display("class status %0d", object.randomize());
    $display("class root %0d", object.x);
    $display("scope status %0d",
             std::randomize(x) with { x * x == limit; soft x == 7;
                                      soft x == preferred; });
    $display("scope root %0d", x);
    limit = 2;
    $display("unsat status %0d",
             std::randomize(x) with { x * x == limit; soft x == 7;
                                      soft x == preferred; });
    $display("unchanged root %0d", x);
    $display("wide status %0d",
             std::randomize(wide) with { $countones(wide) == 1;
                                         $clog2(wide) == logarithm; });
    $display("wide matches %0d", wide == 128'h1000000000000000000000000);
  end
endmodule
