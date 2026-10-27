// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.3 and 18.5.13: std::randomize uses the same exact
// original distribution masses as class randomization. With x > 101 the range
// [100:102] := w keeps its whole mass 3w, so 102 and 103 occur with ratio 3:1
// (expected 144 of 192 draws; the 32-bit weight 2^32-1 must not overflow when
// summed). A hard constraint overrides a soft dist, and a distribution whose
// live weights are all zero fails without changing the randomized variable.
// RUNTIME: scope ranges 1 1
// RUNTIME-NEXT: scope soft dist 1 1
// RUNTIME-NEXT: scope zero weight 1 1

module top;
  bit [7:0] x = 0;
  int unsigned weight = 32'hFFFFFFFF;
  bit valid = 1;
  int unsigned ones = 0;
  int status = 0;
  initial begin
    repeat (192) begin
      valid = valid && std::randomize(x) with {
        x > 101;
        x dist { [100:102] := weight, 103 := weight };
      } == 1 && (x == 102 || x == 103);
      ones += x == 102;
    end
    $display("scope ranges %0d %0d", valid, ones > 118 && ones < 173);
    status = std::randomize(x) with {
      x == 103;
      soft x dist { 102 := weight };
    };
    $display("scope soft dist %0d %0d", status == 1, x == 103);
    weight = 0;
    status = std::randomize(x) with {
      x > 101;
      x dist { [100:102] := weight, 103 := weight };
    };
    $display("scope zero weight %0d %0d", status == 0, x == 103);
  end
endmodule
