// REQUIRES: compiled-sampler
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 18.5.3: state-valued integral endpoints are evaluated on
// each call. := retains the complete original range mass after clipping and
// filtering; :/ retains one item mass. Explicit zero-weight ranges are excluded
// from default even when their bounds change. Reversed and negative bounds,
// 129-bit endpoints/weights, a full 2^64 original range, soft conflicts, modes,
// failed transactional commits and scalar std::randomize use the same policy.
// CHECK: dynamic ranges 1 1 1 1
// CHECK-NEXT: wide dynamic masses 1 1 1
// CHECK-NEXT: live dynamic endpoints 1 1 1
// CHECK-NEXT: clipped empty range 1 1 1
// CHECK-NEXT: disabled dynamic dist 1 1
// CHECK-NEXT: frozen failure 1 1 1
// CHECK-NEXT: frozen success 1 1
// CHECK-NEXT: scalar dynamic scope 1 1
// CHECK-NEXT: scalar clipped failure 1 1
// CHECK-NEXT: default only 1

class C;
  rand bit [7:0] x;
  rand bit [7:0] r;
  rand bit [1:0] d;
  rand byte s;
  rand bit [63:0] full;
  rand bit q;
  rand shortint cast;
  int unsigned low, high, other, filter;
  int unsigned zero_low, zero_high;
  bit signed [128:0] signed_low, signed_high;
  bit [63:0] lo64, hi64;
  bit [128:0] weight;
  int unsigned soft_low, soft_high;
  bit enable;

  constraint hard {
    x >= filter;
    x <= other;
    r >= filter;
    r <= other;
    d >= 2;
    s == -2 || s == 1;
    full == 0 || full == 64'hFFFF_FFFF_FFFF_FFFF;
  }
  constraint weighted {
    x dist { [low:high] := weight, other :/ weight };
    if (enable) r dist { [low:high] :/ weight, other :/ weight };
    d dist { [zero_low:zero_high] :/ 0, default :/ 1 };
    s dist { [signed_low:signed_high] := 1,
             1 :/ 129'd1267650600228229401496703205375 };
    full dist { [lo64:hi64] := 1, 0 :/ 64'h8000_0000_0000_0000 };
    soft q dist { [soft_low:soft_high] :/ weight };
    cast dist { shortint'(byte'(255)) :/ 1 };
  }
endclass

module top;
  C object = new;
  bit valid = 1;
  int x102 = 0, r102 = 0, d2 = 0, sminus2 = 0, full0 = 0;
  int status = 0;
  int unsigned scope_low, scope_high;
  bit [7:0] scope_value;
  bit [1:0] old_d;
  byte old_s;
  bit [63:0] old_full;

  initial begin
    object.low = 100;
    object.high = 102;
    object.other = 103;
    object.filter = 102;
    object.zero_low = 1;
    object.zero_high = 1;
    object.signed_low = -(129'sd1 << 100);
    object.signed_high = -2;
    object.lo64 = 0;
    object.hi64 = 64'hFFFF_FFFF_FFFF_FFFF;
    object.weight = '1;
    object.soft_low = 0;
    object.soft_high = 0;
    object.enable = 1;

    // The dist masses are 3:1 for x (:= over the clipped {102}), 1:1 for r
    // (:/), 1:1 for d (default over {2, 3}), 1:1 for s, and 2:1 for full.
    repeat (192) begin
      valid &= (object.randomize() with { q == 1; }) == 1 &&
               (object.x == 102 || object.x == 103) &&
               (object.r == 102 || object.r == 103) &&
               (object.d == 2 || object.d == 3) &&
               (object.s == -2 || object.s == 1) &&
               object.q == 1 && object.cast == -1;
      x102 += object.x == 102;
      r102 += object.r == 102;
      d2 += object.d == 2;
      sminus2 += object.s == -2;
      full0 += object.full == 0;
    end
    $display("dynamic ranges %0d %0d %0d %0d", valid,
             x102 > 118 && x102 < 173, r102 > 66 && r102 < 126,
             d2 > 66 && d2 < 126);
    $display("wide dynamic masses %0d %0d %0d",
             sminus2 > 66 && sminus2 < 126, full0 > 98 && full0 < 158,
             object.q == 1);

    // Endpoints are read again on the next call: a reversed range and a moved
    // zero-weight range.
    object.low = 103;
    object.high = 102;
    object.zero_low = 2;
    object.zero_high = 2;
    status = object.randomize() with { q == 1; };
    $display("live dynamic endpoints %0d %0d %0d", status == 1, object.d == 3,
             object.x == 102 || object.x == 103);

    // A range clipped to nothing and a disabled conditional dist contribute
    // no mass.
    object.low = 1000;
    object.high = 1001;
    object.enable = 0;
    status = object.randomize() with { q == 1; };
    $display("clipped empty range %0d %0d %0d", status == 1, object.x == 103,
             object.d == 3);

    object.weighted.constraint_mode(0);
    status = object.randomize() with { q == 1; };
    $display("disabled dynamic dist %0d %0d", status == 1, object.q == 1);

    // A frozen variable is a state value for the dist; a failed solve commits
    // nothing.
    object.weighted.constraint_mode(1);
    object.x = 102;
    object.x.rand_mode(0);
    old_d = object.d;
    old_s = object.s;
    old_full = object.full;
    status = object.randomize() with { q == 1; };
    $display("frozen failure %0d %0d %0d", status == 0, object.x == 102,
             object.d == old_d && object.s == old_s && object.full == old_full);
    object.x = 103;
    status = object.randomize() with { q == 1; };
    $display("frozen success %0d %0d", status == 1, object.x == 103);

    // Scalar std::randomize evaluates its endpoints at the call.
    scope_low = 45;
    scope_high = 45;
    status = std::randomize(scope_value) with {
      scope_value dist { [scope_low:scope_high] :/ 1 };
    };
    $display("scalar dynamic scope %0d %0d", status == 1, scope_value == 45);
    scope_low = 300;
    scope_high = 400;
    status = std::randomize(scope_value) with {
      scope_value dist { [scope_low:scope_high] :/ 1 };
    };
    $display("scalar clipped failure %0d %0d", status == 0, scope_value == 45);
    status = std::randomize(scope_value) with {
      scope_value dist { default :/ 1 };
    };
    $display("default only %0d", status == 1);
  end
endmodule
