// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4 and 18.4.2: a static randc array has one class-wide
// cyclic sequence per integral leaf, shared by every instance. Each leaf draws
// from a static managed domain with the compiler-built cycle sampler.
// PLAN: __obelisk_static_randc_domain
// PLAN: simulation.random.cycle_sample
// PLAN-SAME: SAMP

// Two instances alternate on four 2-bit leaves. Every leaf must visit all four
// values once in each group of four draws across both instances, twice over,
// and the constraint-derived sum must follow the drawn leaves.
// RUNTIME: fixed randc static multidimensional cycles 1
class Shared;
  static randc bit [1:0] values [1:0][-2:-1];
  rand bit [7:0] sum;
  constraint relation {
    sum == values[1][-2] + values[1][-1] + values[0][-2] + values[0][-1];
  }
endclass

module top;
  Shared a, b;
  int seen [1:0][-2:-1];
  bit ok;
  task automatic draw(Shared s);
    int expected;
    if (!s.randomize()) ok = 0;
    foreach (seen[i, j]) seen[i][j] |= 1 << s.values[i][j];
    expected = s.values[1][-2] + s.values[1][-1] + s.values[0][-2]
             + s.values[0][-1];
    if (s.sum != expected) ok = 0;
  endtask
  initial begin
    ok = 1;
    a = new;
    b = new;
    a.srandom(13);
    b.srandom(29);
    repeat (2) begin
      foreach (seen[i, j]) seen[i][j] = 0;
      repeat (2) begin
        draw(a);
        draw(b);
      end
      foreach (seen[i, j]) if (seen[i][j] != 15) ok = 0;
    end
    $display("fixed randc static multidimensional cycles %0d", ok);
  end
endmodule
