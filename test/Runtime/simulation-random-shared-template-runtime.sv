// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' | talos-opt --test-talos-print-random-programs -o /dev/null 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.3, 18.5 and 18.8: pure shared arithmetic keeps the
// complete relation, soft preferences, live captures and frozen properties.
// Each of six diamonds computes (e + e) - e modulo 256 and uses its operand
// three times; common subexpression elimination of the constraint template
// leaves one shared graph. Expanding it as a tree would need 3^6 leaf
// occurrences, but the encoded program contains 46 instructions because each
// shared value is stored once and reloaded at its other uses. The duplicate
// hard predicate and its soft copy reuse one value across roots.
// PLAN: simulation.random.solve: version 1 width 16 instructions 46 captures 1 flags [HAS_SOFT, EXACT_UNIFORM, HAS_SAMPLER_PLAN]
// RUNTIME: shared template valid 1

// One diamond computes (e + e) - e modulo 256, which is e again, using the
// value e three times.
`define DIAMOND(e) (((e) + (e)) - (e))
`define CHAIN(e) `DIAMOND(`DIAMOND(`DIAMOND(`DIAMOND(`DIAMOND(`DIAMOND(e))))))

class C;
  rand bit [7:0] x, y;
  bit [7:0] limit = 8'd10;
  constraint legal {
    `CHAIN(x + y) >= limit;
    `CHAIN(x + y) >= limit;
    soft `CHAIN(x + y) >= limit;
  }
  constraint preferred { soft `CHAIN(x + y) < 8'd40; }
endclass

module top;
  C c;
  int valid;
  int low;
  bit [7:0] kept_x, kept_y;
  initial begin
    c = new;
    valid = 1;
    // limit 10: the later soft preference (< 40) is satisfiable.
    repeat (100) begin
      valid &= c.randomize();
      valid &= 8'(c.x + c.y) >= 8'd10 && 8'(c.x + c.y) < 8'd40;
    end
    // Live capture: limit 100 makes the preference unsatisfiable, so only
    // the hard bound remains.
    c.limit = 8'd100;
    repeat (100) begin
      valid &= c.randomize();
      valid &= 8'(c.x + c.y) >= 8'd100;
    end
    // A frozen property is a capture for the other candidate.
    c.y.rand_mode(0);
    c.y = 8'd250;
    c.limit = 8'd10;
    repeat (100) begin
      valid &= c.randomize();
      valid &= c.y == 8'd250 && 8'(c.x + c.y) >= 8'd10 && 8'(c.x + c.y) < 8'd40;
    end
    // Failure keeps every frozen field.
    c.x.rand_mode(0);
    c.x = 8'd1;
    c.y = 8'd2;
    c.limit = 8'd200;
    valid &= c.randomize() == 0;
    valid &= c.x == 8'd1 && c.y == 8'd2;
    $display("shared template valid %0d", valid);
  end
endmodule
