// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=TEXT

// IEEE 1800-2023 11.4.5 and 11.4.8: bitwise operations produce X for an
// indeterminate bit; case equality distinguishes that result from Z.
// Constructor operands and literal operands retain their exact A/B planes.
module top;
  logic [1:0] a;
  bit b;
  covergroup cg(logic [1:0] mask);
    option.cross_retain_auto_bins = 0;
    ca: coverpoint a { bins low = {[0:1]}; bins high = {[2:3]}; }
    cb: coverpoint b { bins zero = {0}; }
    literal_x: cross ca, cb {
      bins selected = literal_x with ((ca & mask) === 2'bx0);
    }
    literal_z: cross ca, cb {
      bins selected = literal_z with ((ca & mask) === 2'bz0);
    }
    constructor_value: cross ca, cb {
      bins selected = constructor_value with ((ca & mask) === mask);
    }
  endgroup
  cg unknown_x, unknown_z;
  int covered, total;
  real percentage;
  initial begin
    unknown_x = new(2'bx0);
    unknown_z = new(2'bz0);
    a = 2;
    unknown_x.sample();
    unknown_z.sample();
    percentage = unknown_x.literal_x.get_inst_coverage(covered, total);
    $display("X literal %.6f %0d %0d", percentage, covered, total);
    percentage = unknown_z.literal_x.get_inst_coverage(covered, total);
    $display("X result from Z %.6f %0d %0d", percentage, covered, total);
    percentage = unknown_x.literal_z.get_inst_coverage(covered, total);
    $display("Z literal %.6f %0d %0d", percentage, covered, total);
    percentage = unknown_x.constructor_value.get_inst_coverage(covered, total);
    $display("X constructor %.6f %0d %0d", percentage, covered, total);
    percentage = unknown_z.constructor_value.get_inst_coverage(covered, total);
    $display("Z constructor %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// $display values: the predicate sees the exact A/B planes of its operands.
// Mask 2'bx0 and mask 2'bz0 both give x0 for 2'b10 & mask (11.4.8), so ===
// against 2'bx0 matches either; === against 2'bz0 never matches (11.4.5), and
// the constructor_value predicate matches only when the mask equals x0.
// CHECK: X literal 100.000000 1 1
// CHECK-NEXT: X result from Z 100.000000 1 1
// CHECK-NEXT: Z literal 0.000000 0 0
// CHECK-NEXT: X constructor 100.000000 1 1
// CHECK-NEXT: Z constructor 0.000000 0 0

// The literal operands keep distinct planes in the report: 2'bx0 is
// aval=2/bval=2 and 2'bz0 is aval=0/bval=2. No cross tuples are materialized.
// JSON-DAG: "role":5{{[^]]*}}"low_aval":2,"low_bval":2,
// JSON-DAG: "role":5{{[^]]*}}"low_aval":0,"low_bval":2,
// JSON: "resolved_functional_tuples":[]

// Bins of the matching predicate are covered; a predicate that matches no
// tuple leaves an empty excluded bin (19.6.1.4).
// TEXT-DAG: cross literal_x: 1/1 (100.00%)
// TEXT-DAG: cross literal_z: 0/0 (0.00%)
// TEXT-DAG: cross constructor_value: 1/1 (100.00%)
// TEXT-DAG: cross constructor_value: 0/0 (0.00%)
// TEXT-DAG: bin selected: 1 [covered]
// TEXT-DAG: bin selected: 0 [excluded] {empty}
