// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4.2: a randc iteration returns each value at most once and
// ends only when none of the values it has not returned satisfies the
// constraints. A state bound that changes between calls changes the legal
// domain, not the constraint set, so the iteration continues. Calls with
// bounds 8, 8, 4, 8, 8, 8, 8, 8 always have an unreturned legal value and
// return one permutation of 0..7.
// CHECK: bound change permutation 1
// A bound of 1 after 0 was returned leaves no legal unreturned value: the
// iteration ends and 0 is returned again.
// CHECK: exhausted domain restarts 1
// With randc x, y and x != y, x is solved first and changes y's legal domain
// on every call; y repeats a value only when x took y's last unreturned
// value.
// CHECK: dependent fields valid 1

class Bounded;
  randc bit [2:0] x;
  int bound = 8;
  constraint c { x < bound; }
endclass

class Pair;
  randc bit [1:0] x, y;
  constraint c { x != y; }
endclass

module top;
  initial begin
    Bounded b = new;
    Pair p;
    int bounds[8] = '{8, 8, 4, 8, 8, 8, 8, 8};
    bit [7:0] seen = 0;
    bit permutation = 1, restarted, valid = 1;
    bit [3:0] ys;
    foreach (bounds[i]) begin
      b.bound = bounds[i];
      permutation &= b.randomize() && b.x < bounds[i] && !seen[b.x];
      seen[b.x] = 1;
    end
    $display("bound change permutation %0d", permutation && seen == 8'hff);

    b = new;
    b.bound = 2;
    void'(b.randomize());
    b.bound = 1;
    if (b.x == 0) begin
      // 0 is already returned; 1 is not legal under the new bound.
      restarted = b.randomize() && b.x == 0;
    end else begin
      // 1 was returned; 0 remains legal and unreturned.
      restarted = b.randomize() && b.x == 0;
      restarted &= b.randomize() && b.x == 0;
    end
    $display("exhausted domain restarts %0d", restarted);

    repeat (32) begin
      // y values returned in the current iteration of y.
      ys = 0;
      p = new;
      repeat (16) begin
        valid &= p.randomize() && p.x != p.y;
        if (ys[p.y]) begin
          // A repeat starts a new iteration, which is legal only when x
          // took the last unreturned y value.
          valid &= (ys | (4'b1 << p.x)) == 4'hf;
          ys = 0;
        end
        ys[p.y] = 1;
        if (ys == 4'hf)
          ys = 0;
      end
    end
    $display("dependent fields valid %0d", valid);
  end
endmodule
