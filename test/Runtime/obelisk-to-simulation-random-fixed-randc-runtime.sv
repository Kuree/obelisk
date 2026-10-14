// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4 and 18.4.2: each fixed array leaf has its own
// legal-domain cycle. Histories follow declared dimensions, survive copying
// and disabled modes, and are published with the entire successful solve.
// PLAN: simulation.class.field {{.*}} : !simulation.unpacked_array<2 : 1 x !simulation.unpacked_array<-1 : 0 x !simulation.string>>
// PLAN-SAME: __obelisk_randc_domain
// PLAN: simulation.random.cycle_sample
// PLAN-SAME: SAMP

// Every leaf visits each of its `bound` legal values exactly once per cycle
// and the wide relation follows the drawn leaves.
// RUNTIME: fixed randc wide multidimensional cycles 1
// A changed bound continues each leaf's iteration: after one value under
// bound 3, the next four draws under bound 5 return each leaf's other values.
// RUNTIME: fixed randc changed bounds continue 1
// A copy keeps the history; a failed randomize leaves history and RNG state.
// RUNTIME: fixed randc copied histories and failure 1
// Disabling one element's rand_mode neither advances nor discards its history.
// RUNTIME: fixed randc element modes preserve histories 1
// Frozen X/Z leaves, disabled constraints, checker mode and explicit variable
// lists leave the other leaves' domains and the rand_mode flags untouched.
// RUNTIME: fixed randc frozen symbols checker and selection 1
class Fixed;
  rand bit [129:0] wide;
  randc logic [31:0] values [2:1][-1:0];
  int unsigned limit = 3;
  bit blocked;
  constraint bounds { foreach (values[i, j]) values[i][j] < limit; }
  constraint relation {
    wide == (130'h1 << 100) + values[2][-1] + values[2][0] + values[1][-1]
          + values[1][0];
  }
  constraint valid { !blocked; }
endclass

module top;
  Fixed c, d;
  int seen [2:1][-1:0];
  int old_value;
  bit ok;
  string before_state;
  bit [129:0] before_wide;

  function automatic bit relation_holds(Fixed f);
    return f.wide == (130'h1 << 100) + f.values[2][-1] + f.values[2][0]
                   + f.values[1][-1] + f.values[1][0];
  endfunction

  function automatic bit same_values(Fixed x, Fixed y);
    foreach (x.values[i, j]) if (x.values[i][j] !== y.values[i][j]) return 0;
    return 1;
  endfunction

  initial begin
    ok = 1;
    for (int bound = 2; bound <= 5; ++bound) begin
      c = new;
      c.srandom(13);
      c.limit = bound;
      repeat (2) begin
        foreach (seen[i, j]) seen[i][j] = 0;
        repeat (bound) begin
          if (!c.randomize()) ok = 0;
          foreach (seen[i, j]) begin
            if ($isunknown(c.values[i][j]) || c.values[i][j] >= bound) ok = 0;
            seen[i][j] |= 1 << c.values[i][j];
          end
          if (!relation_holds(c)) ok = 0;
        end
        foreach (seen[i, j]) if (seen[i][j] != (1 << bound) - 1) ok = 0;
      end
    end
    $display("fixed randc wide multidimensional cycles %0d", ok);

    ok = 1;
    c = new;
    c.srandom(17);
    c.limit = 3;
    if (!c.randomize()) ok = 0;
    foreach (seen[i, j]) seen[i][j] = 1 << c.values[i][j];
    c.limit = 5;
    repeat (4) begin
      if (!c.randomize()) ok = 0;
      foreach (seen[i, j]) begin
        if (seen[i][j][c.values[i][j]]) ok = 0;
        seen[i][j] |= 1 << c.values[i][j];
      end
    end
    foreach (seen[i, j]) if (seen[i][j] != 31) ok = 0;
    $display("fixed randc changed bounds continue %0d", ok);

    ok = 1;
    c.limit = 3;
    if (!c.randomize()) ok = 0;
    d = new c;
    before_state = c.get_randstate();
    before_wide = c.wide;
    c.blocked = 1;
    if (c.randomize() || c.get_randstate() != before_state ||
        c.wide != before_wide) ok = 0;
    if (!same_values(c, d)) ok = 0;
    c.blocked = 0;
    repeat (4) begin
      if (!c.randomize() || !d.randomize()) ok = 0;
      if (!same_values(c, d) || c.wide != d.wide ||
          c.get_randstate() != d.get_randstate()) ok = 0;
    end
    $display("fixed randc copied histories and failure %0d", ok);

    ok = 1;
    d = new c;
    old_value = c.values[2][-1];
    c.values[2][-1].rand_mode(0);
    if (c.values[2][-1].rand_mode() != 0 || c.values[2][0].rand_mode() != 1)
      ok = 0;
    repeat (4)
      if (!c.randomize() || c.values[2][-1] !== old_value) ok = 0;
    c.values[2][-1].rand_mode(1);
    c.values[2][0].rand_mode(0);
    c.values[1][-1].rand_mode(0);
    c.values[1][0].rand_mode(0);
    d.values[2][0].rand_mode(0);
    d.values[1][-1].rand_mode(0);
    d.values[1][0].rand_mode(0);
    d.set_randstate(c.get_randstate());
    repeat (3) begin
      if (!c.randomize() || !d.randomize()) ok = 0;
      if (c.values[2][-1] !== d.values[2][-1] ||
          c.get_randstate() != d.get_randstate()) ok = 0;
    end
    $display("fixed randc element modes preserve histories %0d", ok);

    ok = 1;
    c.values.rand_mode(1);
    c.values[2][-1].rand_mode(0);
    old_value = c.values[2][-1];
    c.values[2][-1] = 32'bxxxxxxxxxxxxxxxx000000000000zzzz;
    c.bounds.constraint_mode(0);
    c.relation.constraint_mode(0);
    if (!c.randomize() ||
        c.values[2][-1] !== 32'bxxxxxxxxxxxxxxxx000000000000zzzz) ok = 0;
    if ($isunknown(c.values[2][0]) || $isunknown(c.values[1][-1]) ||
        $isunknown(c.values[1][0])) ok = 0;
    c.values[2][-1] = old_value;
    c.bounds.constraint_mode(1);
    c.relation.constraint_mode(1);
    c.values.rand_mode(1);
    if (!c.randomize()) ok = 0;
    c.values.rand_mode(0);
    before_state = c.get_randstate();
    if (!c.randomize(null) || c.get_randstate() != before_state) ok = 0;
    before_wide = c.wide;
    if (!c.randomize(values) || c.wide != before_wide) ok = 0;
    foreach (seen[i, j]) if (c.values[i][j].rand_mode() != 0) ok = 0;
    $display("fixed randc frozen symbols checker and selection %0d", ok);
  end
endmodule
