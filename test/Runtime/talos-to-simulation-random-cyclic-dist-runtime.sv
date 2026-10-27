// RUN: %talos --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=PLAN
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4.2 and 18.5.3: ordinary dist contributes positive
// hard support to cyclic projection, then ordinary preferences are resolved
// with the cyclic value frozen. Wide captured weights retain their high words.
// Support shrunk to returned values starts a new iteration; zero total support
// fails without publishing a field, history or RNG update.
// Every cyclic sampling operation carries a compiled SAMP plan.
// PLAN-COUNT-1: simulation.random.cycle_sample {{.*}}SAMP
// CHECK: source randc weighted support cycles 1
// CHECK: source randc live weight domain restart 1
// CHECK: source randc zero weights atomic 1

class Weighted;
  randc bit [31:0] x;
  rand bit [7:0] y;
  bit [31:0] zero_weight;
  bit [63:0] middle = 1;
  bit [129:0] wide_weight = 130'h1 << 100;
  constraint relation { y == x; }
  constraint distribution {
    y dist { [0:1] := zero_weight, [2:3] := middle, [4:5] := wide_weight };
  }
  constraint pref { soft y == 2; }
endclass

module top;
  Weighted c, d;
  bit ok;
  bit [7:0] seen;
  string saved;
  bit [31:0] old_x;
  bit [7:0] old_y;

  initial begin
    c = new;
    d = new;
    c.srandom(55);
    seen = 0;
    ok = 1;
    // Only the values 2..5 have positive weight; each cycle visits all four.
    for (int i = 0; i < 8; i++) begin
      ok &= c.randomize();
      ok &= c.x inside {[2:5]} && c.y == c.x && !seen[c.x];
      seen[c.x] = 1;
      if (i % 4 == 3) begin
        ok &= seen == 8'd60;
        seen = 0;
      end
    end
    $display("source randc weighted support cycles %0d", ok);

    // Zeroing a live weight shrinks the support to values the iteration has
    // already returned, so a new iteration starts as in a fresh object.
    c.middle = 0;
    d.middle = 0;
    d.set_randstate(c.get_randstate());
    ok = c.randomize() && d.randomize();
    ok &= c.x == d.x && c.y == d.y && c.get_randstate() == d.get_randstate();
    seen = 1 << c.x;
    ok &= c.randomize() && c.x inside {[4:5]} && !seen[c.x];
    seen[c.x] = 1;
    ok &= seen == 8'd48;
    $display("source randc live weight domain restart %0d", ok);

    // No positive support fails without publishing a field, history or RNG.
    d = new c;
    c.wide_weight = 0;
    saved = c.get_randstate();
    old_x = c.x;
    old_y = c.y;
    ok = !c.randomize();
    ok &= c.x == old_x && c.y == old_y && c.get_randstate() == saved;
    c.wide_weight = 130'h1 << 100;
    ok &= c.randomize() && d.randomize();
    ok &= c.x == d.x && c.y == d.y && c.get_randstate() == d.get_randstate();
    $display("source randc zero weights atomic %0d", ok);
  end
endmodule
