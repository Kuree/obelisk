// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %talos --std=1800-2023 -O0 -o %t.exe %s
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.3, 18.5.9 and 18.8: proposal discovery over modular
// definition chains, alias representatives, overlapping solve-order slices,
// signed live capture intervals and wide enum domains keeps a compiled
// sampling plan for every randomize call. Cyclic definitions stay in the
// complete relation, and failing or checker-only calls preserve random fields.
// PLAN-COUNT-4: simulation.random.solve{{.*}}SAMP
// RUNTIME: proposal graph valid 1

typedef enum bit [129:0] {
  E0 = 130'h2_0000_0000_0000_0000_0000_0000_0000_0001,
  E1 = 130'h1_0000_0000_0000_0000_0000_0000_0000_0002,
  E2 = 130'h3_FFFF_FFFF_FFFF_FFFF_FFFF_FFFF_FFFF_FFFF
} Wide;

class G;
  rand bit [7:0] a, b, c, d;       // modular definition chain
  rand bit [7:0] x, y, z;          // alias representatives
  rand bit [15:0] word;            // overlapping solve-order slices
  rand Wide e;
  rand byte signed sv;
  byte signed signed_floor = -20, signed_ceil = 15;
  bit flag = 1;
  constraint defs { b == a + 8'd7; c == b + 8'd250; d == c + a; }
  constraint copy { y == x; z == y; x > 8'd200; }
  constraint order {
    solve word[5:0] before word[11:8];
    solve word[7:4] before word[11:8];
    solve word[11:8] before word[15:12];
  }
  constraint span { sv >= signed_floor; sv <= signed_ceil; }
  constraint wide { e inside {E1, E2}; }
endclass

class H;
  rand bit [7:0] lower, upper, mirror;
  bit [7:0] limit = 8'd3;
  constraint cycle { upper == lower + 8'd1; lower == upper + 8'd255; mirror == upper; }
endclass

module top;
  G g;
  H h;
  int valid;
  int ok;
  bit [7:0] sa, sb, sc, sd, sw;
  initial begin
    g = new;
    h = new;
    valid = 1;
    repeat (200) begin
      ok = g.randomize();
      valid &= ok;
      valid &= g.b == 8'(g.a + 7) && g.c == 8'(g.b + 250) && g.d == 8'(g.c + g.a);
      valid &= g.y == g.x && g.z == g.x && g.x > 200;
      valid &= g.sv >= -20 && g.sv <= 15;
      valid &= g.e == E1 || g.e == E2;
      ok = h.randomize();
      valid &= ok;
      valid &= h.upper == 8'(h.lower + 1) && h.mirror == h.upper;
    end
    // Live capture change: the signed interval follows the new captures.
    g.signed_floor = -128; g.signed_ceil = -120;
    repeat (50) begin
      valid &= g.randomize();
      valid &= g.sv >= -128 && g.sv <= -120;
    end
    // Impossible capture interval fails and keeps every random field.
    sa = g.a; sb = g.b; sc = g.c; sd = g.d; sw = g.x;
    g.signed_floor = 10; g.signed_ceil = -10;
    ok = g.randomize();
    valid &= !ok && g.a == sa && g.b == sb && g.c == sc && g.d == sd && g.x == sw;
    // Checker call never changes random fields.
    g.signed_floor = -5; g.signed_ceil = 5;
    sa = g.a;
    ok = g.randomize(null);
    valid &= ok == 0 && g.a == sa;
    valid &= g.randomize();
    valid &= g.randomize(null) == 1;
    $display("proposal graph valid %0d", valid);
  end
endmodule
