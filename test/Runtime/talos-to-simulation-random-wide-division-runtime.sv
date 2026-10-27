// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 11.4.3 and 18.3/18.5: a 455-bit class relation with 130-bit
// dividends, quotients and remainders and a 65-bit divisor read from a live
// capture solves exactly across limb boundaries; a zero divisor makes the
// relation unsatisfiable and randomize() fails without partial class writes.
// CHECK: wide class division PASS

class C;
  rand bit [129:0] n;
  rand bit [64:0] d;
  rand bit [129:0] q;
  rand bit [129:0] r;
  constraint math {
    n == 130'h200000000000000000000000000000009;
    d == top.cap_d;
    q == n / d;
    r == n % d;
  }
endclass

module top;
  int ok;
  bit [64:0] cap_d = 3;
  bit [129:0] saved_q, saved_r;
  C object = new;

  `define DIV_CASE(D, Q, R) \
    cap_d = 65'd``D; \
    ok = object.randomize(); \
    if (ok != 1) $fatal(1, "wide division class solve failed"); \
    if (object.q != 130'h``Q) $fatal(1, "wide quotient lost high limbs"); \
    if (object.r != 130'h``R) $fatal(1, "wide remainder lost high limbs");

  initial begin
    `DIV_CASE(3, aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaad, 2)
    `DIV_CASE(1, 200000000000000000000000000000009, 0)
    `DIV_CASE(5, 66666666666666666666666666666668, 1)
    `DIV_CASE(7, 4924924924924924924924924924924a, 3)
    saved_q = object.q;
    saved_r = object.r;
    cap_d = 65'd0;
    ok = object.randomize();
    if (!(ok == 0 && object.q == saved_q && object.r == saved_r))
      $fatal(1, "wide zero-divisor failure changed class state");
    $display("wide class division PASS");
  end
endmodule
