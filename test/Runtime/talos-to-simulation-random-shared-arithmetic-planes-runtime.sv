// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt --lower-talos-to-sim=opt-level=0 | FileCheck %s --check-prefix=PLAN
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 11.4.13, 18.3 and 18.8: a 96-entry set of four-state values
// expands without copying the shared value/X/Z planes into an expression tree.
// The ODR1 header of the compiled program has 5 random bits (x and answer) and
// 2792 = 29 * 96 + 8 instructions (0x0AE8), linear in the number of entries.
// PLAN: simulation.random.solve
// PLAN-SAME: "ODR1\01\00\18\00\05\00\00\00\E8\0A\00\00
// PLAN-SAME: SAMP
// PLAN-SAME: PLAN

// The captured entries keep RHS wildcard semantics (an X/Z entry bit matches
// any bit); a known match absorbs an unknown comparison and the two-state
// cast consumes an unmatched X result. Frozen X/Z, rand_mode changes,
// failed-solve preservation and inverse membership (answer frozen to 1 forces
// x into the set) must agree between the Z3 plan and ordinary evaluation.
// RUNTIME: shared arithmetic planes valid 1

class C;
  rand logic [3:0] x;
  rand bit answer;
  logic [3:0] entries [96];
  constraint membership { answer == bit'(x inside {entries}); }
endclass

module top;
  C c;
  bit valid;
  initial begin
    c = new;
    valid = 1;
    foreach (c.entries[i]) c.entries[i] = i % 16;
    repeat (32) begin
      valid &= c.randomize();
      valid &= c.answer == bit'(c.x inside {c.entries});
    end
    c.x.rand_mode(0);
    c.x = 4'b1xz0;
    foreach (c.entries[i]) c.entries[i] = 0;
    c.entries[95] = 4'b1xxx;
    valid &= c.randomize();
    valid &= c.answer == 1 && c.x === 4'b1xz0;
    c.entries[95] = 4'b1000;
    valid &= c.randomize();
    valid &= c.answer == 0 && c.x === 4'b1xz0;
    c.answer.rand_mode(0);
    c.answer = 1;
    valid &= c.randomize() == 0;
    valid &= c.answer == 1 && c.x === 4'b1xz0;
    c.answer.rand_mode(1);
    valid &= c.randomize();
    valid &= c.answer == 0 && c.x === 4'b1xz0;
    c.x.rand_mode(1);
    c.answer.rand_mode(0);
    c.answer = 1;
    repeat (8) begin
      valid &= c.randomize();
      valid &= (c.x inside {[0:8]}) && c.answer == 1;
    end
    c.entries[0] = 7;
    c.answer.rand_mode(1);
    repeat (32) begin
      valid &= c.randomize();
      valid &= c.answer == bit'(c.x inside {c.entries});
    end
    $display("shared arithmetic planes valid %0d", valid);
    if (!valid) $fatal(1, "shared arithmetic planes mismatch");
    $finish;
  end
endmodule
