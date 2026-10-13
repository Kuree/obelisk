// RUN: %obelisk --std=1800-2023 -O0 -emit-sim %s -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.sim.mlir
// RUN: obelisk-opt %t.sim.mlir --obelisk-sim-compile-random-plans | diff %t.sim.mlir -
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4.2: the cyclic primitive samples 32-bit projected legal
// domains before ordinary fields and their soft preferences. Every value is
// visited once per cycle, independent of its number of ordinary completions.
// Bounds 1..7 have sparse 32-bit domains and non-power-of-two cycle lengths.
// A full 32-bit cyclic domain with wide inequality completions also retains
// identical histories; projection must not enumerate all those completions.
// Changed bounds restart; plans with more ordinary completions preserve
// projected histories. Failed stages preserve all outputs, and sparse enum
// domain masks are exact.
// Every cyclic sampling operation carries a compiled SAMP plan.
// PLAN-COUNT-4: simulation.random.cycle_sample {{.*}}SAMP
// Hard dist on ordinary fields contributes only positive support to a cycle;
// 130-bit weights, zero weights and failed stages preserve cyclic ordering.
// A class with only a randc byte visits all 256 values of the field twice,
// preserving the unrelated high 24 bits of the aggregate.
// CHECK: cyclic bounds valid 1
// CHECK: cyclic history valid 1
// CHECK: cyclic enum valid 1
// CHECK: cyclic weights valid 1
// CHECK: cyclic unconstrained valid 1

typedef bit [129:0] Wide;
typedef enum bit [31:0] {LOW = 0, HIGH = 32'h80000000, MAX = 32'hFFFFFFFF} Sparse;

// The ordinary payload is exact in one constraint block and a wide inequality
// in another; both project to the same cyclic domain.
class Cycle;
  randc bit [31:0] x;
  rand Wide payload;
  int limit = 1;
  bit blocked;
  constraint bounds { x < limit; }
  constraint exact { payload == ((Wide'(1) << 100) | Wide'(x)); }
  constraint inexact { payload >= ((Wide'(1) << 100) | Wide'(x)); }
  constraint valid { !blocked; }
  constraint pref { soft payload == (Wide'(1) << 100) + 2; }
  function new();
    inexact.constraint_mode(0);
  endfunction
  function void use_inexact();
    exact.constraint_mode(0);
    inexact.constraint_mode(1);
  endfunction
endclass

class SparseCycle;
  randc Sparse e;
  rand Wide payload;
  constraint relation { payload == ((Wide'(1) << 100) | Wide'(e)); }
endclass

class Weighted;
  randc bit [31:0] x;
  rand bit [3:0] low;
  rand Wide payload;
  Wide zero_weight = 0;
  bit [63:0] middle = 1;
  Wide wide_weight = Wide'(1) << 100;
  constraint bounds { x < 6; }
  constraint exact { payload == ((Wide'(1) << 100) | Wide'(x)); }
  constraint tie { low == x; }
  constraint distribution {
    low dist { [0:1] := zero_weight, [2:3] := middle, [4:5] := wide_weight };
  }
endclass

class Plain;
  randc bit [7:0] r;
  rand bit [23:0] hi;
endclass

module top;
  Cycle o, f, g, h, fresh, twin, other;
  SparseCycle s;
  Weighted w, wtwin;
  Plain u;
  bit ok;
  bit [7:0] seen;
  bit [255:0] seen_all;
  string st_a, st_b;

  initial begin
    // Each bound has a cycle of its own length, visited exactly once per cycle.
    ok = 1;
    for (int limit = 1; limit < 8; limit++) begin
      o = new;
      o.srandom(13);
      o.limit = limit;
      seen = 0;
      for (int i = 0; i < 2 * limit; i++) begin
        ok &= o.randomize();
        ok &= o.x < limit && !seen[o.x];
        ok &= o.payload == ((Wide'(1) << 100) | Wide'(o.x));
        seen[o.x] = 1;
        if ((i + 1) % limit == 0) begin
          ok &= seen == (1 << limit) - 1;
          seen = 0;
        end
      end
    end
    $display("cyclic bounds valid %0d", ok);

    // A changed bound restarts the history exactly like a fresh object.
    ok = 1;
    o = new;
    o.srandom(13);
    o.limit = 3;
    ok &= o.randomize();
    st_a = o.get_randstate();
    fresh = new;
    fresh.set_randstate(st_a);
    fresh.limit = 5;
    o.limit = 5;
    ok &= o.randomize() && fresh.randomize();
    ok &= o.x == fresh.x && o.payload == fresh.payload;
    ok &= o.get_randstate() == fresh.get_randstate();

    // A plan with more ordinary completions keeps the projected history: the
    // rest of the cycle visits the same values.
    f = new o;
    g = new o;
    g.use_inexact();
    repeat (4) begin
      ok &= f.randomize() && g.randomize();
      ok &= f.x == g.x && f.x < 5;
    end

    // Without a bound the domain is the full 32 bits; restart equals fresh, and
    // the wide inequality plan retains the same history.
    st_b = o.get_randstate();
    h = new o;
    h.bounds.constraint_mode(0);
    fresh = new;
    fresh.set_randstate(st_b);
    fresh.bounds.constraint_mode(0);
    ok &= h.randomize() && fresh.randomize();
    ok &= h.x == fresh.x && h.payload == fresh.payload;
    ok &= h.get_randstate() == fresh.get_randstate();
    other = new h;
    other.use_inexact();
    twin = new h;
    repeat (3) begin
      ok &= twin.randomize() && other.randomize();
      ok &= twin.x == other.x;
    end

    // A failed stage with frozen ordinary bits or a false block publishes
    // neither fields, histories nor RNG: it matches an untouched twin.
    twin = new o;
    f = new o;
    f.payload = 42;
    f.payload.rand_mode(0);
    st_a = f.get_randstate();
    ok &= !f.randomize();
    ok &= f.get_randstate() == st_a && f.x == o.x && f.payload == 42;
    f.payload.rand_mode(1);
    f.payload = o.payload;
    ok &= f.randomize() && twin.randomize();
    ok &= f.x == twin.x && f.payload == twin.payload;
    ok &= f.get_randstate() == twin.get_randstate();
    g = new o;
    twin = new o;
    g.blocked = 1;
    st_a = g.get_randstate();
    ok &= !g.randomize();
    ok &= g.get_randstate() == st_a && g.x == o.x;
    g.blocked = 0;
    ok &= g.randomize() && twin.randomize();
    ok &= g.x == twin.x && g.payload == twin.payload;
    ok &= g.get_randstate() == twin.get_randstate();
    $display("cyclic history valid %0d", ok);

    // A sparse enum domain is exact: three values, once per cycle.
    ok = 1;
    s = new;
    s.srandom(13);
    seen = 0;
    for (int i = 0; i < 6; i++) begin
      ok &= s.randomize();
      ok &= s.e inside {LOW, HIGH, MAX};
      ok &= s.payload == ((Wide'(1) << 100) | Wide'(s.e));
      ok &= !seen[s.e == LOW ? 0 : s.e == HIGH ? 1 : 2];
      seen[s.e == LOW ? 0 : s.e == HIGH ? 1 : 2] = 1;
      if (i % 3 == 2) begin
        ok &= seen == 7;
        seen = 0;
      end
    end
    $display("cyclic enum valid %0d", ok);

    // Only positive dist weights support the cycle; zero total support fails
    // and leaves the history untouched.
    ok = 1;
    w = new;
    w.srandom(13);
    seen = 0;
    for (int i = 0; i < 8; i++) begin
      ok &= w.randomize();
      ok &= w.x inside {[2:5]} && !seen[w.x];
      seen[w.x] = 1;
      if (i % 4 == 3) begin
        ok &= seen == 60;
        seen = 0;
      end
    end
    void'(w.randomize());
    wtwin = new w;
    w.zero_weight = 0;
    w.middle = 0;
    w.wide_weight = 0;
    st_a = w.get_randstate();
    ok &= !w.randomize();
    ok &= w.get_randstate() == st_a;
    w.middle = 1;
    w.wide_weight = Wide'(1) << 100;
    ok &= w.randomize() && wtwin.randomize();
    ok &= w.x == wtwin.x && w.payload == wtwin.payload;
    ok &= w.get_randstate() == wtwin.get_randstate();
    $display("cyclic weights valid %0d", ok);

    // An unconstrained byte visits all 256 values twice and keeps other bits.
    ok = 1;
    u = new;
    u.srandom(13);
    u.hi = 24'hDEADBE;
    u.hi.rand_mode(0);
    seen_all = 0;
    for (int i = 0; i < 512; i++) begin
      ok &= u.randomize();
      ok &= !seen_all[u.r] && u.hi == 24'hDEADBE;
      seen_all[u.r] = 1;
      if (i % 256 == 255) begin
        ok &= seen_all == {256{1'b1}};
        seen_all = 0;
      end
    end
    $display("cyclic unconstrained valid %0d", ok);
  end
endmodule
