// RUN: %obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4.2 and 18.10: persistent domains restart live cyclic
// permutations when bounds, blocks or inline constraints change. Scalar fields
// span the full 32-bit range while small legal sets are sampled directly.
// Checker calls and inactive fields preserve histories. Failed solves publish
// neither fields, histories nor RNG. Static histories are shared by instances.
// Shallow copies preserve RNG, modes and cyclic states as required by 8.12.
// PLAN-DAG: __obelisk_randc_domain
// PLAN-DAG: __obelisk_static_randc_domain
// Every cyclic sampling operation carries a compiled SAMP plan.
// PLAN-COUNT-5: simulation.random.cycle_sample {{.*}}SAMP
// CHECK: source randc complete sparse cycles 1
// CHECK: source randc changed bound restart 1
// CHECK: source randc copied history and failed solve 1
// CHECK: source randc checker modes and live blocks 1
// CHECK: source randc inline and unconstrained domains 1
// CHECK: source randc static shared domain and history 1

class Cycle;
  randc bit [31:0] x;
  rand bit [7:0] y;
  int limit = 3;
  bit blocked;
  constraint bounds { x < limit; }
  constraint relation { y == x; }
  constraint valid { !blocked; }
  constraint pref { soft y == 2; }
endclass

class Bare;
  randc bit [31:0] x;
endclass

class Shared;
  static randc bit [31:0] x;
  int limit = 3;
  bit blocked;
  constraint bounds { x < limit; }
  constraint valid { !blocked; }
endclass

module top;
  Cycle c, d;
  Bare a, b;
  Shared p, q;
  string saved;
  bit ok;
  bit [7:0] seen;
  bit [31:0] old_x;
  bit [7:0] old_y;
  int i;

  initial begin
    c = new;
    d = new;
    c.srandom(99);
    d.srandom(99);
    ok = 1;
    // Bounds 2..7 have sparse 32-bit domains and every value is visited once
    // per cycle of the live domain length.
    for (int n = 2; n <= 7; n++) begin
      c.limit = n;
      seen = 0;
      for (i = 0; i < n; i++) begin
        ok &= c.randomize();
        ok &= c.x < n && c.y == c.x && !seen[c.x];
        seen[c.x] = 1;
      end
      ok &= seen == (1 << n) - 1;
    end
    $display("source randc complete sparse cycles %0d", ok);

    // A changed bound restarts the cycle in both objects identically.
    c.limit = 3;
    void'(c.randomize());
    c.limit = 5;
    d.limit = 5;
    d.set_randstate(c.get_randstate());
    ok = c.randomize() && d.randomize();
    ok &= c.x == d.x && c.y == d.y && c.get_randstate() == d.get_randstate();
    seen = 1 << c.x;
    for (i = 1; i < 5; i++) begin
      ok &= c.randomize();
      ok &= c.x < 5 && !seen[c.x];
      seen[c.x] = 1;
    end
    ok &= seen == 31;
    $display("source randc changed bound restart %0d", ok);

    c.limit = 1;
    ok = c.randomize() && c.x == 0 && c.y == 0;
    c.limit = 5;
    void'(c.randomize());
    d = new c;
    c.blocked = 1;
    old_x = c.x;
    old_y = c.y;
    saved = c.get_randstate();
    ok &= !c.randomize();
    ok &= c.x == old_x && c.y == old_y && c.get_randstate() == saved;
    c.blocked = 0;
    ok &= c.randomize() && d.randomize();
    ok &= c.x == d.x && c.y == d.y && c.get_randstate() == d.get_randstate();
    $display("source randc copied history and failed solve %0d", ok);

    d = new c;
    c.x.rand_mode(0);
    saved = c.get_randstate();
    old_x = c.x;
    old_y = c.y;
    ok = c.randomize(null);
    ok &= c.get_randstate() == saved && c.x == old_x && c.y == old_y;
    ok &= c.randomize() && c.x == old_x && c.y == old_x;
    d.set_randstate(c.get_randstate());
    c.x.rand_mode(1);
    ok &= c.randomize() && d.randomize();
    ok &= c.x == d.x && c.y == d.y && c.get_randstate() == d.get_randstate();
    c.bounds.constraint_mode(0);
    c.relation.constraint_mode(0);
    ok &= c.randomize();
    c.bounds.constraint_mode(1);
    c.relation.constraint_mode(1);
    c.limit = 3;
    seen = 0;
    for (i = 0; i < 3; i++) begin
      ok &= c.randomize();
      ok &= c.x < 3 && !seen[c.x];
      seen[c.x] = 1;
    end
    ok &= seen == 7;
    $display("source randc checker modes and live blocks %0d", ok);

    a = new;
    b = new;
    a.srandom(111);
    seen = 0;
    ok = 1;
    for (i = 0; i < 3; i++) begin
      ok &= a.randomize() with { x inside {[0:2]}; };
      ok &= a.x < 3 && !seen[a.x];
      seen[a.x] = 1;
    end
    ok &= seen == 7;
    b.set_randstate(a.get_randstate());
    ok &= a.randomize() && b.randomize();
    ok &= a.x == b.x && a.get_randstate() == b.get_randstate();
    ok &= a.randomize() with { x == 32'd2147483648; };
    ok &= a.x == 32'd2147483648;
    b = new;
    b.set_randstate(a.get_randstate());
    ok &= a.randomize() && b.randomize();
    ok &= a.x == b.x && a.get_randstate() == b.get_randstate();
    $display("source randc inline and unconstrained domains %0d", ok);

    p = new;
    q = new;
    p.srandom(222);
    q.srandom(333);
    seen = 0;
    ok = p.randomize() && p.x < 3;
    seen[p.x] = 1;
    ok &= q.randomize() && q.x < 3 && !seen[q.x];
    seen[q.x] = 1;
    q.blocked = 1;
    saved = q.get_randstate();
    old_x = q.x;
    ok &= !q.randomize() && q.x == old_x && q.get_randstate() == saved;
    q.blocked = 0;
    ok &= p.randomize() && p.x < 3 && !seen[p.x];
    seen[p.x] = 1;
    ok &= seen == 7;
    p.limit = 5;
    q.limit = 5;
    seen = 0;
    for (i = 0; i < 5; i++) begin
      if (i & 1) ok &= p.randomize();
      else ok &= q.randomize();
      ok &= p.x < 5 && !seen[p.x];
      seen[p.x] = 1;
    end
    ok &= seen == 31;
    $display("source randc static shared domain and history %0d", ok);
  end
endmodule
