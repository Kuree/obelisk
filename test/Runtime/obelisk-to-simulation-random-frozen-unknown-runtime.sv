// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.8: a property with rand_mode(0) is a state variable, so
// randomize() leaves its stored X/Z untouched and the compiled solver sees it
// as a constant. Two-state casts and known-absorbing operators (6.11.2,
// Clause 11) consume the stored unknown bits; the active candidate stays
// two-state.

typedef bit [3:0] B4;
typedef bit [129:0] B130;

class C;
  rand logic [3:0] x;
  rand bit [3:0] q;
  bit [2:0] mode;
  constraint c {
    if (mode == 1) q == B4'(~x);
    else if (mode == 2) q == B4'(x | 4'b100);
    else if (mode == 3) q == (x & 4'b100);
    else if (mode == 4) q == x;
    else q == 0;
  }
endclass

class W;
  rand logic [129:0] x;
  rand bit [129:0] q;
  constraint c { q == B130'(~x); }
endclass

module top;
  C c;
  W w;
  int ok;
  bit [3:0] saved;
  initial begin
    c = new;
    c.x = 4'b10xz;
    c.x.rand_mode(0);

    // ~(10xz) = 01xz; the two-state cast maps X and Z to 0.
    c.mode = 1;
    ok = c.randomize();
    $display("complement ok=%0d q=%0d x=%b", ok, c.q, c.x);
    // CHECK: complement ok=1 q=4 x=10xz

    // x | 4'b100 = 11xz.
    c.mode = 2;
    ok = c.randomize();
    $display("absorb ok=%0d q=%0d x=%b", ok, c.q, c.x);
    // CHECK-NEXT: absorb ok=1 q=12 x=10xz

    // x & 4'b100 = 0 in the known bit and X/Z elsewhere; no unknown survives
    // an unconstrained-zero comparison against a known 0.
    c.mode = 3;
    ok = c.randomize();
    $display("known ok=%0d q=%0d x=%b", ok, c.q, c.x);
    // CHECK-NEXT: known ok=1 q=0 x=10xz

    // q == x with x unknown is not satisfiable by a two-state q (11.4.5); the
    // failed call leaves q unchanged (18.6.3).
    c.q = 13;
    saved = c.q;
    c.mode = 4;
    ok = c.randomize();
    $display("atomic ok=%0d same=%0d x=%b", ok, c.q == saved, c.x);
    // CHECK-NEXT: atomic ok=0 same=1 x=10xz

    // randomize(null) only checks: q is a state variable and satisfies 1.
    c.mode = 1;
    c.q = 4;
    c.q.rand_mode(0);
    ok = c.randomize(null);
    $display("check ok=%0d q=%0d x=%b", ok, c.q, c.x);
    // CHECK-NEXT: check ok=1 q=4 x=10xz

    // A randomize(args) list makes x active for this call only (18.11);
    // its stored X/Z is replaced by two-state bits and q == ~x holds.
    c.q = 1;
    ok = c.randomize(x);
    $display("temp ok=%0d x=%b q=%0d mode=%0d", ok, c.x, c.q, c.x.rand_mode());
    // CHECK-NEXT: temp ok=1 x=1110 q=1 mode=0

    // Once active, stored X/Z does not taint the candidate.
    c.x = 4'b10xz;
    c.x.rand_mode(1);
    c.q.rand_mode(1);
    c.mode = 2;
    ok = c.randomize();
    $display("active ok=%0d unknown=%0d match=%0d", ok, $isunknown(c.x),
             c.q == B4'(c.x | 4'b100));
    // CHECK-NEXT: active ok=1 unknown=0 match=1

    // Switching a property back to a state variable captures its current
    // stored unknown symbols again.
    c.x = 4'b10xz;
    c.x.rand_mode(0);
    c.mode = 1;
    ok = c.randomize();
    $display("rebind ok=%0d q=%0d x=%b", ok, c.q, c.x);
    // CHECK-NEXT: rebind ok=1 q=4 x=10xz

    // The same capture on a 130-bit value (multi-word planes).
    w = new;
    w.x = {1'b1, 125'b0, 4'bxxxx};
    w.x.rand_mode(0);
    ok = w.randomize();
    $display("wide ok=%0d match=%0d x=%0d", ok,
             w.q == ((130'h1 << 129) - 130'h10), w.x === {1'b1, 125'b0, 4'bxxxx});
    // CHECK-NEXT: wide ok=1 match=1 x=1
  end
endmodule
