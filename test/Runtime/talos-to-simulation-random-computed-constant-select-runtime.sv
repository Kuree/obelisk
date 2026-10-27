// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 11.4.3, 11.5.1, 18.5.12 and 20.9: a constant part-select of
// a computed value (a function result) keeps separate value, X and Z planes.
// Concatenation preserves symbols, bitwise AND with a known 0 consumes unknown
// bits, and division by a non-constant taints every bit. The two-state cast
// consumes the selected X/Z symbols. The state index is not part of the
// selection, so even an unknown index must not affect it.

typedef bit [3:0] B4;

function automatic logic [7:0] join8(logic [3:0] high, logic [3:0] low);
  return {high, low};
endfunction

function automatic logic [7:0] mask8(logic [7:0] x);
  return x & 8'd240;
endfunction

function automatic logic [7:0] div8(logic [7:0] x, int divisor);
  return x / divisor;
endfunction

class C;
  rand logic [7:0] x;
  rand bit [3:0] out;
  rand bit [3:0] nz;
  rand bit [3:0] nx;
  logic signed [31:0] index;
  int which;
  int divisor;
  bit active;
  constraint fixed { if (active) x == 8'd165; }
  constraint selected {
    if (which == 0) {
      out == B4'(join8(x[7:4], x[3:0])[7:4]);
      nz == $countbits(join8(x[7:4], x[3:0])[7:4], 1'bz);
      nx == $countbits(join8(x[7:4], x[3:0])[7:4], 1'bx);
    } else if (which == 1) {
      out == B4'(mask8(x)[7:4]);
      nz == $countbits(mask8(x)[7:4], 1'bz);
      nx == $countbits(mask8(x)[7:4], 1'bx);
    } else {
      out == B4'(div8(x, divisor)[7:4]);
      nz == $countbits(div8(x, divisor)[7:4], 1'bz);
      nx == $countbits(div8(x, divisor)[7:4], 1'bx);
    }
  }
endclass

module top;
  C c;
  task automatic show(string tag, int ok);
    $display("%s ok=%0d out=%0d nz=%0d nx=%0d x=%b", tag, ok, c.out, c.nz,
             c.nx, c.x);
  endtask
  initial begin
    c = new;
    c.x = 8'b1010xz01;
    c.x.rand_mode(0);

    // The high nibble 1010 is known even though the low nibble is not.
    c.index = 0;
    show("concat-low-index", c.randomize());
    // CHECK: concat-low-index ok=1 out=10 nz=0 nx=0 x=1010xz01
    c.index = 4;
    show("concat-high-index", c.randomize());
    // CHECK-NEXT: concat-high-index ok=1 out=10 nz=0 nx=0 x=1010xz01

    c.which = 1;
    c.index = 0;
    show("and-low-index", c.randomize());
    // CHECK-NEXT: and-low-index ok=1 out=10 nz=0 nx=0 x=1010xz01
    c.index = 4;
    show("and-high-index", c.randomize());
    // CHECK-NEXT: and-high-index ok=1 out=10 nz=0 nx=0 x=1010xz01

    // x / 1 is all X when x has unknown bits: all four selected bits are X.
    c.which = 2;
    c.divisor = 1;
    c.index = 0;
    show("div-low-index", c.randomize());
    // CHECK-NEXT: div-low-index ok=1 out=0 nz=0 nx=4 x=1010xz01
    c.index = 4;
    show("div-high-index", c.randomize());
    // CHECK-NEXT: div-high-index ok=1 out=0 nz=0 nx=4 x=1010xz01

    c.x = 8'd165;
    c.index = 0;
    show("known-low-index", c.randomize());
    // CHECK-NEXT: known-low-index ok=1 out=10 nz=0 nx=0 x=10100101
    c.index = 4;
    show("known-high-index", c.randomize());
    // CHECK-NEXT: known-high-index ok=1 out=10 nz=0 nx=0 x=10100101

    c.divisor = 0;
    show("div-zero", c.randomize());
    // CHECK-NEXT: div-zero ok=1 out=0 nz=0 nx=4 x=10100101

    // Out-of-range and unknown state indices are not read by the selection.
    c.index = 5;
    show("index-5", c.randomize());
    // CHECK-NEXT: index-5 ok=1 out=0 nz=0 nx=4 x=10100101
    c.index = 32'bz;
    show("index-z", c.randomize());
    // CHECK-NEXT: index-z ok=1 out=0 nz=0 nx=4 x=10100101

    c.index = 4;
    c.divisor = 1;
    c.active = 1;
    c.x.rand_mode(1);
    show("active-div", c.randomize());
    // CHECK-NEXT: active-div ok=1 out=10 nz=0 nx=0 x=10100101
    c.which = 0;
    c.index = 0;
    show("active-concat", c.randomize());
    // CHECK-NEXT: active-concat ok=1 out=10 nz=0 nx=0 x=10100101
  end
endmodule
