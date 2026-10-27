// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// RUN: not %t --seed=13 +coupled 2>&1 | FileCheck %s --check-prefix=COUPLED
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.3: each dist weighs its own expression. Distributions on
// independent fields factor, so sixteen fields with three items each are
// sampled field by field rather than over all 3^16 item combinations. With
// weights 0 := 1, [1:254] :/ 1 and 255 := 1 each field takes 0 and 255 with
// probability 1/3; 4096 field samples give about 1365 of each.
// CHECK: independent dist zeros 1{{[0-9][0-9][0-9]}} maxes 1{{[0-9][0-9][0-9]}} valid 1

// Distributions coupled by a relation keep the exact joint measure. Past the
// sampler's combination budget a solve reports exhausted resources instead
// of enumerating 2^17 item combinations.
// COUPLED: out of resources

`define FIELD(n) f``n dist {0 := 1, [1:254] :/ 1, 255 := 1};

class Independent;
  rand bit [7:0] f0, f1, f2, f3, f4, f5, f6, f7;
  rand bit [7:0] f8, f9, f10, f11, f12, f13, f14, f15;
  constraint c {
    `FIELD(0) `FIELD(1) `FIELD(2) `FIELD(3) `FIELD(4) `FIELD(5) `FIELD(6)
    `FIELD(7) `FIELD(8) `FIELD(9) `FIELD(10) `FIELD(11) `FIELD(12)
    `FIELD(13) `FIELD(14) `FIELD(15)
  }
endclass

`define WEIGH(n) b``n dist {0 := 1, 1 := 2};
`define COUNT(n) int'(b``n)

class Coupled;
  rand bit b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14,
      b15, b16;
  constraint c {
    `WEIGH(0) `WEIGH(1) `WEIGH(2) `WEIGH(3) `WEIGH(4) `WEIGH(5) `WEIGH(6)
    `WEIGH(7) `WEIGH(8) `WEIGH(9) `WEIGH(10) `WEIGH(11) `WEIGH(12)
    `WEIGH(13) `WEIGH(14) `WEIGH(15) `WEIGH(16)
    `COUNT(0) + `COUNT(1) + `COUNT(2) + `COUNT(3) + `COUNT(4) + `COUNT(5) +
        `COUNT(6) + `COUNT(7) + `COUNT(8) + `COUNT(9) + `COUNT(10) +
        `COUNT(11) + `COUNT(12) + `COUNT(13) + `COUNT(14) + `COUNT(15) +
        `COUNT(16) == 8;
  }
endclass

module top;
  initial begin
    Independent ind = new;
    Coupled coupled = new;
    int zeros = 0, maxes = 0, valid = 1;
    bit [7:0] values[16];
    if ($test$plusargs("coupled")) begin
      void'(coupled.randomize());
      $finish;
    end
    repeat (256) begin
      valid &= ind.randomize();
      values = '{ind.f0, ind.f1, ind.f2, ind.f3, ind.f4, ind.f5, ind.f6,
                 ind.f7, ind.f8, ind.f9, ind.f10, ind.f11, ind.f12, ind.f13,
                 ind.f14, ind.f15};
      foreach (values[i]) begin
        zeros += values[i] == 0;
        maxes += values[i] == 255;
      end
    end
    $display("independent dist zeros %0d maxes %0d valid %0d", zeros, maxes,
             valid);
  end
endmodule
