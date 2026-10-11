// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.9: sample every legal joint combination equally.
// Four live-capture modular roots form a sparse component beside 128 free bits.
// The old cyclic fallback repeatedly selected only one root. Repeated exact
// sampling must cover all roots and both values of every wide unconstrained bit.
// A cyclic pair of capture-dependent definitions has 2^32 legal combinations
// and exercises exact counting instead of a bounded assignment table.
// CHECK: uniform valid 1
// CHECK-NEXT: all roots 1
// CHECK-NEXT: wide all ones 1
// CHECK-NEXT: wide all zeros 1
// CHECK-NEXT: cycle all ones 1
// CHECK-NEXT: cycle all zeros 1

module top;
  bit [31:0] x = 0;
  bit [31:0] y = 0;
  bit [31:0] cycle_ones = 0;
  bit [31:0] cycle_zeros = 0;
  bit [127:0] free = 0;
  bit [31:0] limit = 49;
  bit [31:0] seen = 0;
  bit valid = 1;
  bit [127:0] ones = 0;
  bit [127:0] zeros = 0;
  initial begin
    repeat (64) begin
      valid = valid && randomize(x, free) with { x * x == limit; } == 1;
      valid = valid && x * x == limit;
      seen = seen | (32'(x == 7) * 1) | (32'(x == 32'h7ffffff9) * 2) |
             (32'(x == 32'h80000007) * 4) | (32'(x == 32'hfffffff9) * 8);
      ones = ones | free;
      zeros = zeros | ~free;
    end
    repeat (64) begin
      valid = valid && randomize(x, y) with { y == x + limit; x == y - limit; } == 1;
      valid = valid && y == x + limit;
      cycle_ones = cycle_ones | x;
      cycle_zeros = cycle_zeros | ~x;
    end
    $display("uniform valid %0d", valid);
    $display("all roots %0d", seen == 15);
    $display("wide all ones %0d", ones == 128'hffffffffffffffffffffffffffffffff);
    $display("wide all zeros %0d", zeros == 128'hffffffffffffffffffffffffffffffff);
    $display("cycle all ones %0d", cycle_ones == 32'hffffffff);
    $display("cycle all zeros %0d", cycle_zeros == 32'hffffffff);
  end
endmodule
