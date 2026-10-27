// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: z3

// IEEE 1800-2023 18.5.13.2: disable only earlier preferences that reference
// the target. Check block modes, derived and inline priorities, candidate
// guards, precise packed selections, and scope randomization.
// CHECK: disable soft PASS

class Defaults;
  rand bit [2:0] x;
  rand bit [2:0] y;
  constraint defaults { soft x == 3; soft y == 2; }
  constraint drop { disable soft x; }
endclass

// A later derived preference keeps priority over the inherited disable soft.
class Later extends Defaults;
  constraint later { soft x == 5; }
endclass

// An enclosing implication or if condition is not a reference to its target.
class Guarded;
  rand bit p;
  rand bit q;
  bit enabled;
  constraint defaults {
    p -> soft q;
    soft (!p || q);
  }
  constraint drop {
    if (enabled) disable soft p;
  }
endclass

// Only the selected bit of a packed variable is disabled.
class PackedPaths;
  rand bit [1:0] x;
  constraint defaults { soft x[0] == 1; soft x[1] == 1; }
  constraint drop { disable soft x[0]; }
endclass

module random_disable_soft;
  initial begin
    Defaults d = new;
    Later l = new;
    Guarded g = new;
    PackedPaths paths = new;
    bit [7:0] seen = 0;
    bit [3:0] path_seen = 0;
    bit [2:0] scope_x, scope_y;

    repeat (128) begin
      if (!d.randomize()) $fatal(1, "disable soft failed");
      if (d.y != 2) $fatal(1, "unrelated soft constraint discarded");
      seen[d.x] = 1;
      if (!paths.randomize()) $fatal(1, "packed disable soft failed");
      if (!paths.x[1]) $fatal(1, "disjoint packed soft constraint discarded");
      path_seen[paths.x] = 1;
    end
    if (seen != 8'hff) $fatal(1, "disable soft failed to extend the domain");
    if (path_seen != 4'b1100) $fatal(1, "packed disable soft domain incorrect");

    d.drop.constraint_mode(0);
    repeat (8) begin
      if (!d.randomize() || d.x != 3 || d.y != 2)
        $fatal(1, "disabled drop block still discarded preferences");
    end
    if (!l.randomize() || l.x != 5 || l.y != 2)
      $fatal(1, "disable soft discarded a higher-priority preference");
    if (!d.randomize() with { disable soft x; x == 6; } || d.x != 6)
      $fatal(1, "inline disable soft failed");

    g.enabled = 1;
    repeat (8) begin
      g.q = 0;
      if (!g.randomize(p) with { p == 1; } || g.q != 0)
        $fatal(1, "guard-only disable changed state");
      if (!g.randomize() with { p == 1; } || !g.q)
        $fatal(1, "disable soft counted an enclosing implication guard");
    end

    repeat (8) begin
      if (!randomize(scope_x, scope_y) with {
            soft scope_x == 3;
            soft scope_y == 2;
            disable soft scope_x;
            soft scope_x == 5;
          } || scope_x != 5 || scope_y != 2)
        $fatal(1, "scope disable soft priority incorrect");
    end
    $display("disable soft PASS");
  end
endmodule
