// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.3: integral expression weights retain their unsigned
// values, zero exclusions and original range normalization. Companions do
// not multiply target mass; preceding solve layers supply conditional state.
// PLAN: simulation.random.solve
// PLAN-SAME: SAMP

// Weight x with x < 4: masses 0:1:2:3 over 2400 draws, and y <= x holds.
// RUNTIME: candidate target weight and companion projection 1
// Per-value x+1 over [lo:hi] plus 3 :/ 1: masses 1:2:3:5 (overlap is additive),
// checked over 600 draws.
// RUNTIME: candidate captured original range mass 1
// A reversed range read from live properties is an empty item, not an error.
// RUNTIME: candidate reversed live endpoint 1
// The default complement divides its weight by the legal complement size:
// masses 3:1:2:3 for 0 :/ 1 and default :/ x.
// RUNTIME: candidate default complement weight 1
// 129-bit power-of-two weights keep exact 2:1 masses.
// RUNTIME: candidate 129 bit unsigned masses 1
// Zero total weight fails transactionally; checker mode, rand_mode and
// constraint_mode all honor the weights.
// RUNTIME: candidate modes checker and transactional failure 1
// A weight computed as -1 is read as an unsigned 32-bit value, not as zero.
// RUNTIME: candidate signed weight interpreted unsigned 1
// Overlapping per-value and per-range items add: masses 0:1:2:7.
// RUNTIME: candidate overlapping per range masses 1
// A soft distribution whose weights are all zero is discarded.
// RUNTIME: candidate discarded zero soft distribution 1
typedef bit [128:0] WideWeight;

class Weighted;
  rand bit [2:0] x;
  rand bit [3:0] y;
  randc bit [1:0] cycle;
  int lo = 0;
  int hi = 7;
  constraint companion { y <= x; }
endclass

class Target extends Weighted;
  constraint distribution { x < 4; x dist { [0:3] :/ x }; }
endclass

class Captured extends Weighted;
  constraint distribution { x < 4; x dist { [lo:hi] := (x + 1), 3 :/ 1 }; }
endclass

class Complement extends Weighted;
  constraint distribution { x < 4; x dist { 0 :/ 1, default :/ x }; }
endclass

class Wide extends Weighted;
  constraint distribution {
    x < 2;
    x dist { [0:1] :/ (WideWeight'(1) << (128 - x)) };
  }
endclass

class ZeroTotal extends Weighted;
  constraint distribution { x dist { [0:7] :/ (x ^ x) }; }
endclass

class Unsigned extends Weighted;
  constraint distribution { x == 0; x dist { 0 :/ (x - 1) }; }
endclass

class Overlap extends Weighted;
  constraint distribution { x < 4; x dist { [0:3] := x, 3 :/ (x + 1) }; }
endclass

class ZeroSoft extends Weighted;
  constraint distribution { x < 4; soft x dist { [0:3] :/ (x ^ x) }; }
endclass

module top;
  Target t;
  Captured cap;
  Complement comp;
  Wide wide;
  ZeroTotal zero;
  Unsigned uns;
  Overlap ovl;
  ZeroSoft zsoft;
  int n0, n1, n2, n3;
  bit ok;
  bit [2:0] previous;
  bit [1:0] previous_cycle;

  task automatic reset_counts();
    n0 = 0; n1 = 0; n2 = 0; n3 = 0;
  endtask

  task automatic tally(Weighted c);
    if (c.x == 0) n0++;
    if (c.x == 1) n1++;
    if (c.x == 2) n2++;
    if (c.x == 3) n3++;
  endtask

  initial begin
    t = new;
    cap = new;
    comp = new;
    wide = new;
    zero = new;
    uns = new;
    ovl = new;
    zsoft = new;

    ok = 1;
    repeat (2400) begin
      ok &= t.randomize() && t.x > 0 && t.x < 4 && t.y <= t.x;
      tally(t);
    end
    ok &= n0 == 0 && n1 > 300 && n1 < 500 && n2 > 670 && n2 < 930 &&
          n3 > 1080 && n3 < 1320;
    $display("candidate target weight and companion projection %0d", ok);

    reset_counts();
    ok = 1;
    repeat (600) begin
      ok &= cap.randomize() && cap.x < 4 && cap.y <= cap.x;
      tally(cap);
    end
    ok &= n0 > 20 && n0 < 90 && n1 > 62 && n1 < 156 && n2 > 110 &&
          n2 < 218 && n3 > 212 && n3 < 334;
    $display("candidate captured original range mass %0d", ok);

    cap.lo = 7;
    cap.hi = 0;
    ok = 1;
    repeat (24) ok &= cap.randomize() && cap.x < 4 && cap.y <= cap.x;
    $display("candidate reversed live endpoint %0d", ok);

    reset_counts();
    ok = 1;
    repeat (2400) begin
      ok &= comp.randomize() && comp.x < 4;
      tally(comp);
    end
    ok &= n0 > 660 && n0 < 950 && n1 > 190 && n1 < 360 && n2 > 400 &&
          n2 < 650 && n3 > 660 && n3 < 950;
    $display("candidate default complement weight %0d", ok);

    reset_counts();
    ok = 1;
    repeat (2400) begin
      ok &= wide.randomize() && wide.x < 2;
      tally(wide);
    end
    ok &= n0 > 1440 && n0 < 1760 && n1 > 640 && n1 < 960;
    $display("candidate 129 bit unsigned masses %0d", ok);

    previous = zero.x;
    previous_cycle = zero.cycle;
    ok = !zero.randomize() && zero.x == previous && zero.cycle == previous_cycle;
    zero.distribution.constraint_mode(0);
    ok &= zero.randomize();
    t.x = 0;
    ok &= !t.randomize(null) && t.x == 0;
    t.x = 2;
    t.y = 0;
    ok &= t.randomize(null) && t.x == 2;
    t.x.rand_mode(0);
    t.x = 0;
    ok &= !t.randomize() && t.x == 0;
    t.distribution.constraint_mode(0);
    ok &= t.randomize() && t.x == 0;
    $display("candidate modes checker and transactional failure %0d", ok);

    ok = 1;
    repeat (24) ok &= uns.randomize() && uns.x == 0;
    $display("candidate signed weight interpreted unsigned %0d", ok);

    reset_counts();
    ok = 1;
    repeat (2400) begin
      ok &= ovl.randomize() && ovl.x > 0 && ovl.x < 4;
      tally(ovl);
    end
    ok &= n0 == 0 && n1 > 190 && n1 < 360 && n2 > 400 && n2 < 650 &&
          n3 > 1460 && n3 < 1740;
    $display("candidate overlapping per range masses %0d", ok);

    ok = 1;
    repeat (24) ok &= zsoft.randomize() && zsoft.x < 4;
    $display("candidate discarded zero soft distribution %0d", ok);
    $finish();
  end
endmodule
