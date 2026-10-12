// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 11.4.3, 11.5.1, 18.5.12 and 20.9: a bit-select of a computed
// value (a function result, A.8.4 function_subroutine_call [range_expression])
// keeps separate value, X and Z planes. Concatenation preserves symbols,
// bitwise AND with a known 0 consumes unknown bits, and division by a
// non-constant taints every bit. The two-state cast consumes X/Z, while a bad
// address fails randomize() without committing any proposed property.

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
      out == B4'(join8(x[7:4], x[3:0])[index]);
      nz == $countbits(join8(x[7:4], x[3:0])[index], 1'bz);
      nx == $countbits(join8(x[7:4], x[3:0])[index], 1'bx);
    } else if (which == 1) {
      out == B4'(mask8(x)[index]);
      nz == $countbits(mask8(x)[index], 1'bz);
      nx == $countbits(mask8(x)[index], 1'bx);
    } else {
      out == B4'(div8(x, divisor)[index]);
      nz == $countbits(div8(x, divisor)[index], 1'bz);
      nx == $countbits(div8(x, divisor)[index], 1'bx);
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

    // Concatenation keeps the Z at bit 2 and the X at bit 3.
    c.index = 2;
    show("concat-low", c.randomize());
    // CHECK: concat-low ok=1 out=0 nz=1 nx=0 x=1010xz01
    c.index = 3;
    show("concat-high", c.randomize());
    // CHECK-NEXT: concat-high ok=1 out=0 nz=0 nx=1 x=1010xz01

    // AND with a known 0 consumes the unknown bits.
    c.which = 1;
    c.index = 0;
    show("and-low", c.randomize());
    // CHECK-NEXT: and-low ok=1 out=0 nz=0 nx=0 x=1010xz01
    c.index = 4;
    show("and-high", c.randomize());
    // CHECK-NEXT: and-high ok=1 out=0 nz=0 nx=0 x=1010xz01

    // Division by a non-zero divisor still taints every bit of an unknown x.
    c.which = 2;
    c.divisor = 1;
    c.index = 0;
    show("div-low", c.randomize());
    // CHECK-NEXT: div-low ok=1 out=0 nz=0 nx=1 x=1010xz01
    c.index = 4;
    show("div-high", c.randomize());
    // CHECK-NEXT: div-high ok=1 out=0 nz=0 nx=1 x=1010xz01

    // A known operand is selected exactly.
    c.x = 8'd165;
    c.index = 0;
    show("known-low", c.randomize());
    // CHECK-NEXT: known-low ok=1 out=1 nz=0 nx=0 x=10100101
    c.index = 4;
    show("known-high", c.randomize());
    // CHECK-NEXT: known-high ok=1 out=0 nz=0 nx=0 x=10100101

    // Division by zero is all X; the cast to bit consumes it.
    c.divisor = 0;
    show("div-zero", c.randomize());
    // CHECK-NEXT: div-zero ok=1 out=0 nz=0 nx=1 x=10100101

    // Index 8 and a Z address are evaluation errors: nothing is committed.
    c.index = 8;
    show("range", c.randomize());
    // CHECK-NEXT: range ok=0 out=0 nz=0 nx=1 x=10100101
    c.index = 32'bz;
    show("unknown-address", c.randomize());
    // CHECK-NEXT: unknown-address ok=0 out=0 nz=0 nx=1 x=10100101

    // An active x is solved to 165 and the selection is exact.
    c.index = 4;
    c.divisor = 1;
    c.active = 1;
    c.x.rand_mode(1);
    show("active-div", c.randomize());
    // CHECK-NEXT: active-div ok=1 out=0 nz=0 nx=0 x=10100101
    c.which = 0;
    c.index = 0;
    show("active-concat", c.randomize());
    // CHECK-NEXT: active-concat ok=1 out=1 nz=0 nx=0 x=10100101
  end
endmodule
