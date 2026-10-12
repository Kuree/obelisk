// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.11 permits pure system functions in constraints; they
// stay bidirectional and add no solve-order priority. Four-state declarations
// still have two-state active candidates, so the initial X/Z of x and y must
// not taint $clog2, $countbits or their relation in the compiled plan. Duplicate
// $countbits controls are a set, and X/Z controls match no random bit (20.9).
// The class and scope forms must both find x == 32'h8000_0000, y == 1.

class C;
  rand logic [31:0] x;
  rand logic [31:0] y;
  constraint rules {
    $countones(x) == 1;
    $clog2(x) == 31;
    $countbits(x, 1'b1, 1'b1) == 1;
    $countbits(x, 1'b0) == 31;
    $countbits(x, 1'bx, 1'bz) == 0;
    $onehot(x) == 1;
    $onehot0(x) == 1;
    $isunknown(x) == 0;
    $unsigned(x) == 32'h80000000;
    $signed(x) == 32'sh80000000;
    y == $countones(x);
  }
endclass

module top;
  C object;
  logic [31:0] x;
  logic [31:0] y;
  initial begin
    object = new;
    $display("class status %0d", object.randomize());
    $display("class values %0d %0d", object.x, object.y);
    $display("scope status %0d", randomize(x, y) with {
      $countones(x) == 1;
      $clog2(x) == 31;
      $countbits(x, 1'b1, 1'b1) == 1;
      $countbits(x, 1'b0) == 31;
      $countbits(x, 1'bx, 1'bz) == 0;
      $onehot(x) == 1;
      $onehot0(x) == 1;
      $isunknown(x) == 0;
      $unsigned(x) == 32'h80000000;
      $signed(x) == 32'sh80000000;
      y == $countones(x);
    });
    $display("scope values %0d %0d", x, y);
  end
endmodule
// CHECK: class status 1
// CHECK-NEXT: class values 2147483648 1
// CHECK-NEXT: scope status 1
// CHECK-NEXT: scope values 2147483648 1
