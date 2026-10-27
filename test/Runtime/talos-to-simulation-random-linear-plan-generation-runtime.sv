// REQUIRES: compiled-sampler
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt --lower-talos-to-sim=opt-level=0 | FileCheck %s --check-prefix=PLAN

// IEEE 1800-2023 18.5, 18.5.9 and 18.8: many atoms, uneven field widths,
// live and dense cardinalities, truncated count results and logarithm endpoints
// use exact compiled plans. Disabling an atom retains every other restriction;
// failed solves and checker-only calls preserve state.
// CHECK: linear plan generation PASS

// Every one of the eight randomize() calls lowers to a solve carrying a
// compiled sampling plan; none falls back to a plan-less runtime solve.
// PLAN-COUNT-8: simulation.random.solve{{(_wide)?}} {{.*}}SAMP{{.*}}PLAN

`define ATOM(n) constraint p``n { lanes[n] == live[n]; }
`define ATOM10(n) `ATOM(n``0) `ATOM(n``1) `ATOM(n``2) `ATOM(n``3) `ATOM(n``4) \
  `ATOM(n``5) `ATOM(n``6) `ATOM(n``7) `ATOM(n``8) `ATOM(n``9)

class C;
  rand bit [32:0] lanes;
  rand bit [129:0] wide;
  bit [32:0] live = 33'h155aa55aa;
  bit [129:0] wanted = 130'h200000000000000000000000000000003;

  `ATOM(0) `ATOM(1) `ATOM(2) `ATOM(3) `ATOM(4) `ATOM(5) `ATOM(6) `ATOM(7)
  `ATOM(8) `ATOM(9) `ATOM10(1) `ATOM10(2) `ATOM(30) `ATOM(31) `ATOM(32)
  constraint w { wide == wanted; }
endclass

module top;
  bit [23:0] value, saved;
  int count, exponent;
  C c;

  initial begin
    c = new;
    if (!c.randomize())
      $fatal(1, "many-atom solve failed");
    if (c.lanes != c.live || c.wide != c.wanted)
      $fatal(1, "mixed field widths mismatch");

    c.p0.constraint_mode(0);
    if (!c.randomize() with { lanes[0] == !live[0]; })
      $fatal(1, "disabled atom failed");
    if (c.lanes[32:1] != c.live[32:1] || c.lanes[0] == c.live[0] ||
        c.wide != c.wanted)
      $fatal(1, "disabled atom committed invalid bits");
    c.p0.constraint_mode(1);
    if (c.randomize(null))
      $fatal(1, "checker ignored restored atom");
    if (!c.randomize())
      $fatal(1, "restored atom solve failed");

    for (int i = 0; i < 4; i++) begin
      case (i)
        0: begin count = 12; exponent = 24; end
        1: begin count = 16; exponent = 24; end
        2: begin count = 0; exponent = 0; end
        3: begin count = 24; exponent = 24; end
      endcase
      if (!randomize(value) with { $countones(value) == count;
                                   $clog2(value) == exponent; })
        $fatal(1, "dynamic cardinality solve failed");
      if ($countones(value) != count || $clog2(value) != exponent)
        $fatal(1, "dynamic cardinality sampled invalid value");
    end

    if (!randomize(value) with { $countones(value) == 12; })
      $fatal(1, "dense static cardinality failed");
    if ($countones(value) != 12)
      $fatal(1, "dense static cardinality mismatch");

    if (!randomize(value) with { value == 24'd255; 3'($countones(value)) == 3'b0; })
      $fatal(1, "truncated popcount comparison failed");
    if (value != 24'd255)
      $fatal(1, "truncated popcount comparison mismatch");

    count = 1;
    exponent = 23;
    if (!randomize(value) with { $countones(value) == count;
                                 $clog2(value) == exponent; })
      $fatal(1, "power-of-two logarithm failed");
    if (value != 24'd8388608)
      $fatal(1, "power-of-two logarithm mismatch");

    saved = value;
    exponent = 24;
    if (randomize(value) with { $countones(value) == count;
                                $clog2(value) == exponent; })
      $fatal(1, "impossible logarithm succeeded");
    if (value != saved)
      $fatal(1, "failed logarithm changed state");
    $display("linear plan generation PASS");
    $finish;
  end
endmodule
