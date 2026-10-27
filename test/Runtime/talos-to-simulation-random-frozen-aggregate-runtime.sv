// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4 and 18.8: elements of a fixed-size array and members of
// a structure have their own rand_mode. A frozen element keeps its X/Z bits,
// and the constraint reading it as ~elem sees them only through the two-state
// conversion boundary (X/Z become 0), while an active element is sampled
// two-state and rebound after rand_mode(1). Mixed active and frozen leaves,
// struct member snapshots and the ordinary string member are all checked.
// CHECK: frozen array and struct planes PASS

typedef bit [3:0] b4_t;
typedef struct {
  rand logic [3:0] a [0:1];
  rand logic [3:0] x;
  string text;
} s_t;

class C;
  rand logic [3:0] p [0:1];
  rand s_t s;
  rand b4_t a0, a1, sx, sa0, sa1;
  constraint c {
    a0 == b4_t'(~p[0]);
    a1 == b4_t'(~p[1]);
    sx == b4_t'(~s.x);
    sa0 == b4_t'(~s.a[0]);
    sa1 == b4_t'(~s.a[1]);
  }
endclass

module top;
  C c;
  int ok;
  initial begin
    c = new;
    c.p[0] = 4'b10xz;
    c.p[1] = 4'bxxxx;
    c.p[0].rand_mode(0);
    c.s.x = 4'bx011;
    c.s.a[0] = 4'b11zz;
    c.s.a[1] = 4'bxxxx;
    c.s.text = "keep";
    c.s.x.rand_mode(0);
    c.s.a[0].rand_mode(0);
    ok = c.randomize();
    if (!ok || c.a0 != 4 || c.p[0] !== 4'b10xz || $isunknown(c.p[1]) || c.a1 != b4_t'(~c.p[1])
        || c.sx != 4 || c.s.x !== 4'bx011 || c.sa0 != 0 || c.s.a[0] !== 4'b11zz
        || $isunknown(c.s.a[1]) || c.sa1 != b4_t'(~c.s.a[1]) || c.s.text != "keep")
      $fatal(1, "mixed frozen array or structure operands");

    c.p[0].rand_mode(1);
    c.p[1].rand_mode(0);
    c.p[1] = 4'b10xz;
    c.s.x.rand_mode(1);
    c.s.a[0].rand_mode(1);
    c.s.a[1].rand_mode(0);
    c.s.a[1] = 4'b1xz;
    ok = c.randomize();
    if (!ok || $isunknown(c.p[0]) || c.a0 != b4_t'(~c.p[0]) || c.a1 != 4 || c.p[1] !== 4'b10xz
        || $isunknown(c.s.x) || c.sx != b4_t'(~c.s.x) || $isunknown(c.s.a[0])
        || c.sa0 != b4_t'(~c.s.a[0]) || c.sa1 != 8 || c.s.a[1] !== 4'b1xz || c.s.text != "keep")
      $fatal(1, "per-element modes did not rebind unknown symbols");
    $display("frozen array and struct planes PASS");
  end
endmodule
