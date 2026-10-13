// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// RUN: %obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=PLAN
// REQUIRES: compiled-sampler

// IEEE 1800-2023 6.12.1 and 20.5: numeric conversions of computed real values.
// Live addition captures exercise positive and negative half ties, zero, a
// fractional boundary and low-bit conversion of a value above 2^65. Active
// numeric-coordinate sampling must agree with the native casts and $rtoi.
// Checker-only calls and frozen failed solves preserve assignments and RNG.
// CHECK: computed real source valid 1
//
// The numeric casts of the computed sum are exported as compiled sampler
// plans on both coordinate widths of the class.
// PLAN-COUNT-2: simulation.random.solve_wide {{.*}}SAMP

class C;
  rand real x;
  real point = 1.5;
  real addend = 1;
  int rounded = 3;
  int truncated = 2;
  constraint c {
    x == point;
    int'(x + addend) == rounded;
    $rtoi(x + addend) == truncated;
  }
endclass

module top;
  C c = new;
  bit valid = 1;
  real oldValue;
  string beforeState;
  initial begin
    for (int k = 0; k < 6; k++) begin
      if (k == 0) begin
        c.point = 1.5; c.addend = 1; c.rounded = 3; c.truncated = 2;
      end else if (k == 1) begin
        c.point = -1.5; c.addend = -1; c.rounded = -3; c.truncated = -2;
      end else if (k == 2) begin
        c.point = 0; c.addend = 0; c.rounded = 0; c.truncated = 0;
      end else if (k == 3) begin
        c.point = 2.5; c.addend = 1; c.rounded = 4; c.truncated = 3;
      end else if (k == 4) begin
        c.point = 1.25; c.addend = 0.25; c.rounded = 2; c.truncated = 1;
      end else begin
        c.point = 3.6893488147419103e+19; c.addend = 1; c.rounded = 0; c.truncated = 0;
      end
      valid &= c.randomize();
      valid &= c.x == c.point && int'(c.x + c.addend) == c.rounded &&
               $rtoi(c.x + c.addend) == c.truncated;
      valid &= c.randomize(null);
    end

    // A frozen failed solve preserves the assignment and the random stream.
    c.x.rand_mode(0);
    oldValue = c.x;
    beforeState = c.get_randstate();
    c.rounded = 99;
    valid &= !c.randomize() && c.x == oldValue && c.get_randstate() == beforeState;
    $display("computed real source valid %0d", valid);
  end
endmodule
