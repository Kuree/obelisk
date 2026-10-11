// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.3, 18.4 and 18.8: class enum properties of 65 and 130
// bits, as a scalar, as a packed array and as an unpacked array, draw only
// enumerators; a live constraint bounds the 130-bit enum; an inactive array
// element keeps its non-enumerator value while the active ones stay in the
// domain; reactivating it draws an enumerator again.
// CHECK: wide class enum domains PASS

typedef enum bit [64:0] {
  A0 = 65'h0,
  A1 = 65'h8000000000000000,
  A2 = 65'h10000000000000000,
  A3 = 65'h10000000000000005
} e65_t;
typedef enum bit [129:0] {
  B0 = 130'h0,
  B1 = 130'h10000000000000000,
  B2 = 130'h200000000000000000000000000000000,
  B3 = 130'h200000000000000000000000000000003
} e130_t;

class C;
  rand e130_t e;
  rand e65_t [1:0] p;
  rand e65_t u [-1:0];
  constraint bounded { e <= top.limit; }
endclass

module top;
  bit [129:0] limit = 130'h10000000000000000;
  int zeros, ok;
  C object = new;

  function automatic bit in_e65(e65_t v);
    return v == A0 || v == A1 || v == A2 || v == A3;
  endfunction

  initial begin
    repeat (128) begin
      ok = object.randomize();
      if (ok != 1) $fatal(1, "wide class enum solve failed");
      if (!(object.e == B0 || object.e == B1)) $fatal(1, "wide enum escaped its live bound");
      if (object.e == B0) zeros++;
      if (!(in_e65(object.p[0]) && in_e65(object.p[1]) && in_e65(object.u[-1]) && in_e65(object.u[0])))
        $fatal(1, "wide enum array produced a non-enumerator");
    end
    if (!(zeros > 40 && zeros < 88)) $fatal(1, "wide enum conditional ranks are biased");

    object.u[-1] = e65_t'(65'd7);
    object.u[-1].rand_mode(0);
    repeat (12) begin
      if (object.randomize() != 1) $fatal(1, "wide enum frozen-hole solve failed");
      if (object.u[-1] != e65_t'(65'd7)) $fatal(1, "wide enum sampler changed a frozen nonmember");
      if (!in_e65(object.u[0])) $fatal(1, "wide enum sampler lost an active array domain");
    end
    object.u[-1].rand_mode(1);
    if (object.randomize() != 1) $fatal(1, "wide enum reactivation failed");
    if (!in_e65(object.u[-1])) $fatal(1, "wide enum reactivation retained an illegal value");
    $display("wide class enum domains PASS");
  end
endmodule
