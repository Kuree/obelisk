// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.7.1, 18.5.12 and 18.12: selected array elements keep
// independent static sibling references and the guard algebra tells STATE
// from RANDOM even for a self comparison, so only a TRUE state disjunct can
// absorb the error from a[bad]. Scope randomization selects 65-bit leaves of
// a 130-bit array by a live index; an invalid index preserves every proposed
// assignment.

class H;
  rand bit [3:0] a [0:1];
  rand bit [3:0] y;
  int index;
  int bad = 2;
  constraint guard {
    a[index] == a[1] || a[bad] == 0;
    y == 5;
  }
endclass

module top;
  H h;
  bit [64:0] a [-1:0];
  bit [64:0] out;
  int index;
  int good;

  initial begin
    h = new;
    h.index = 0;
    h.a[0] = 0;
    h.a[1] = 0;
    h.y = 11;

    // a[1] is random: RANDOM || ERROR is an error.
    h.a[0].rand_mode(0);
    $display("random-sibling ok=%0d y=%0d", h.randomize(), h.y);
    // CHECK: random-sibling ok=0 y=11

    // Both elements are state and equal: TRUE absorbs the error.
    h.a[1].rand_mode(0);
    $display("state-siblings ok=%0d y=%0d", h.randomize(), h.y);
    // CHECK-NEXT: state-siblings ok=1 y=5

    // FALSE || ERROR is an error.
    h.a[1] = 1;
    $display("false-sibling ok=%0d y=%0d", h.randomize(), h.y);
    // CHECK-NEXT: false-sibling ok=0 y=5

    // a[1] == a[1] mentions a random variable, so it is RANDOM, not TRUE.
    h.a[1].rand_mode(1);
    h.index = 1;
    $display("self-compare ok=%0d y=%0d", h.randomize(), h.y);
    // CHECK-NEXT: self-compare ok=0 y=5

    h.bad = 1;
    $display("valid-bad ok=%0d y=%0d", h.randomize(), h.y);
    // CHECK-NEXT: valid-bad ok=1 y=5

    // With every property inactive, randomize() only checks the constraints.
    h.a.rand_mode(0);
    h.y.rand_mode(0);
    h.bad = 2;
    $display("check-pass ok=%0d y=%0d", h.randomize(), h.y);
    // CHECK-NEXT: check-pass ok=1 y=5
    h.y = 4;
    $display("check-fail ok=%0d y=%0d", h.randomize(), h.y);
    // CHECK-NEXT: check-fail ok=0 y=4

    // Scope randomization with 65-bit leaves and an index in [-1:0].
    good = 0;
    for (int iteration = 0; iteration < 12; iteration++) begin
      index = -1;
      good += randomize(a, out) with {
        a[-1] == 65'h1_0000_0000_0000_0003;
        a[0] == 65'h0_8000_0000_0000_0005;
        out == a[index];
      } && out == 65'h1_0000_0000_0000_0003 && a[0] == 65'h0_8000_0000_0000_0005;
      index = 0;
      good += randomize(a, out) with {
        a[-1] == 65'h1_0000_0000_0000_0003;
        a[0] == 65'h0_8000_0000_0000_0005;
        out == a[index];
      } && out == 65'h0_8000_0000_0000_0005 && a[-1] == 65'h1_0000_0000_0000_0003;
    end
    $display("scope good=%0d", good);
    // CHECK-NEXT: scope good=24

    // Index 1 is out of range: the call fails and keeps every value.
    index = 1;
    $display("scope-bad ok=%0d out=%h a=%h,%h",
             randomize(a, out) with { out == a[index]; }, out, a[-1], a[0]);
    // CHECK-NEXT: scope-bad ok=0 out=08000000000000005 a=10000000000000003,08000000000000005
  end
endmodule
