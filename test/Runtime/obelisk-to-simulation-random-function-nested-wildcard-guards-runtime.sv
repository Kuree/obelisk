// REQUIRES: compiled-sampler
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 12.5, 12.5.1, 18.5.11 and 18.5.12:
// A normal case consumes exact X/Z symbols returned by a nested wildcard
// case. Safe captures retain the inner wildcard semantics beneath the
// symbol-consuming outer comparison. Selected state branches discard later
// label errors; unmatched X, live modes and checker-only calls remain exact.
// CHECK: nested wildcard function guards PASS

class D;
  rand bit [3:0] out;
  logic [3:0] value;
  logic [3:0] labels[0:0];
  int bad = 9;
  bit use_x;
  bit return_z;

  function logic [3:0] inner_z(logic [3:0] v);
    casez (v)
      4'b1zzz: begin
        if (return_z) return 4'b10xz;
        else return 4'b10xx;
      end
      labels[bad]: return 4'b10;
      default: return 4'b11;
    endcase
  endfunction

  function logic [3:0] inner_x(logic [3:0] v);
    casex (v)
      4'b1zzz: begin
        if (return_z) return 4'b10xz;
        else return 4'b10xx;
      end
      labels[bad]: return 4'b10;
      default: return 4'b11;
    endcase
  endfunction

  function bit [3:0] outer_case(logic [3:0] v, bit x_mode);
    logic [3:0] computed;
    if (x_mode) computed = inner_x(v);
    else computed = inner_z(v);
    case (computed)
      4'b10xz: return 1;
      4'b10xx: return 2;
      labels[bad]: return 3;
      default: return 4;
    endcase
  endfunction

  constraint relation { out == outer_case(value, use_x); }
endclass

module top;
  D d;
  initial begin
    d = new;
    d.value = 4'bz000;
    d.use_x = 0;
    d.return_z = 1;
    if (!d.randomize() || d.out != 1 || d.value !== 4'bz000)
      $fatal(1, "nested casez selector lost return Z");
    d.return_z = 0;
    if (!d.randomize() || d.out != 2 || d.value !== 4'bz000)
      $fatal(1, "nested casez selector lost return X");
    d.value = 4'bx000;
    d.use_x = 1;
    d.return_z = 1;
    if (!d.randomize() || d.out != 1 || d.value !== 4'bx000)
      $fatal(1, "nested casex selector did not discard later errors");
    if (!d.randomize(null))
      $fatal(1, "nested symbol checker failed");
    d.out.rand_mode(0);
    d.return_z = 0;
    if (d.randomize() || d.out != 1 || d.value !== 4'bx000)
      $fatal(1, "nested frozen output mismatch accepted");
    d.out.rand_mode(1);
    d.use_x = 0;
    if (d.randomize() || d.out != 1 || d.value !== 4'bx000)
      $fatal(1, "nested casez accepted non-wildcard X");
    d.relation.constraint_mode(0);
    if (!d.randomize() || d.value !== 4'bx000)
      $fatal(1, "disabled nested case block failed");
    d.relation.constraint_mode(1);
    d.value = 4'b100x;
    if (!d.randomize() || d.out != 2 || d.value !== 4'b100x)
      $fatal(1, "nested selector did not consume return unknown planes");
    $display("nested wildcard function guards PASS");
    $finish;
  end
endmodule
