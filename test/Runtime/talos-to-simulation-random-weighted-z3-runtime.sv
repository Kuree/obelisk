// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.3: the original := range mass survives filtering by
// other constraints (x in {102,103} stays 3:1), :/ keeps one range mass, and a
// split default complement remains one item (d in {1,2} stays 1:1). 129-bit
// live weights and a 129-bit unweighted companion exercise exact target
// projections. A state guard activates a dist only in its branch, a dist under
// an implication on a solve-before predecessor applies only when the guard
// holds, and a conflicting hard inline constraint discards a soft dist.
// Multiple dists on one variable retain legal solutions (product of masses,
// implementation-defined by 18.5.3), and zero or maximum weights are unsigned
// without overflowing normalized masses. A zero live weight removes its value.
// RUNTIME: weighted ranges 1 1 1 1
// RUNTIME-NEXT: weighted ordering 1 1 1 1
// RUNTIME-NEXT: live weights 1 1 1 1

typedef bit [128:0] big_t;

class C;
  rand bit [7:0] x, r;
  rand bit [1:0] d;
  rand bit t;
  rand big_t data;
  rand bit g, z, s, m;
  big_t low, high;
  bit enable;
  constraint constraints {
    x > 101;
    x dist { [100:102] := low, 103 := low };
    r > 101;
    r <= 103;
    if (enable) r dist { [100:102] :/ 1, 103 :/ 1 };
    d >= 1;
    d <= 2;
    d dist { 1 :/ 1, default :/ 1 };
    t dist { 0 := low, 1 := high };
    t -> data == 0;
    solve g before z;
    g -> z dist { 0 := low, 1 := high };
    soft s dist { 0 := low };
    m dist { 0 :/ 1, 1 :/ 3 };
    m dist { 0 :/ 3, 1 :/ 1 };
  }
endclass

module top;
  C object = new;
  bit valid = 1;
  int unsigned x102, r102, d2, t1, g1, gz1, m1;
  int status;
  initial begin
    object.low = big_t'(1) << 64;
    object.high = big_t'(3) << 64;
    object.enable = 1;
    repeat (192) begin
      valid = valid && object.randomize() with { s == 1; } == 1 &&
              (object.x == 102 || object.x == 103) &&
              (object.r == 102 || object.r == 103) &&
              (object.d == 1 || object.d == 2) &&
              (object.t == 0 || object.data == 0) && object.s == 1;
      x102 += object.x == 102;
      r102 += object.r == 102;
      d2 += object.d == 2;
      t1 += object.t == 1;
      g1 += object.g == 1;
      gz1 += object.g == 1 && object.z == 1;
      m1 += object.m == 1;
    end
    $display("weighted ranges %0d %0d %0d %0d", valid, x102 > 118 && x102 < 173,
             r102 > 66 && r102 < 126, d2 > 66 && d2 < 126);
    $display("weighted ordering %0d %0d %0d %0d", t1 > 118 && t1 < 173,
             g1 > 66 && g1 < 126, gz1 > 45 && gz1 < 100, m1 > 66 && m1 < 126);

    object.enable = 0;
    object.high = 0;
    object.low = {129{1'b1}};
    status = object.randomize() with { s == 1; };
    $display("live weights %0d %0d %0d %0d", status == 1, object.t == 0,
             object.s == 1, object.g == 0 || object.z == 0);
  end
endmodule
