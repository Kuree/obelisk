// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov -o %t.json
// RUN: FileCheck %s --check-prefix=POINT < %t.json
// RUN: FileCheck %s --check-prefix=CROSS < %t.json
// RUN: FileCheck %s --check-prefix=DIAG < %t.json

// IEEE 1800-2023 19.5.1 distributes half-open real intervals into array bins.
// A partition may lie entirely between adjacent shortreal values. With no value
// associated with such a bin, 19.11.1 excludes it from the coverage denominator,
// and 19.6 forms automatic crosses from the remaining contributing bins.
// Twelve 3e-8 partitions fall into eight bins; the final bin takes the remainder
// and the last interval includes its high endpoint. Four samples exercise both
// precision and final-bin endpoint handling.
module top;
  shortreal actual;
  bit side;
  covergroup cg;
    cp: coverpoint actual {
      type_option.real_interval = 0.00000003;
      bins pieces[8] = {[1.0:1.00000035762786865234375]};
    }
    other: coverpoint side { bins zero = {0}; bins one = {1}; }
    product: cross cp, other;
  endgroup
  cg cov;
  int covered, total;
  real percentage;
  initial begin
    cov = new;
    actual = 1.0; cov.sample();
    actual = 1.00000011920928955078125; cov.sample();
    actual = 1.0000002384185791015625; cov.sample();
    actual = 1.00000035762786865234375; cov.sample();
    percentage = cov.cp.get_inst_coverage(covered, total);
    $display("partitions %.6f %0d %0d", percentage, covered, total);
    percentage = cov.product.get_inst_coverage(covered, total);
    $display("product %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: partitions 100.000000 3 3
// CHECK-NEXT: product 50.000000 3 6

// The coverpoint reports 3/3: only bins holding a shortreal value contribute
// (counts 1,0,0,1,0,0,0,2), and each empty bin carries the excluded flag (128).
// POINT: "name":"cp"
// POINT-SAME: "covered":3,"total":3,
// POINT-SAME: "name":"pieces[0]"{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"excluded":false,"contributing":true,
// POINT-SAME: "name":"pieces[1]"{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false,
// POINT-SAME: "name":"pieces[2]"{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false,
// POINT-SAME: "name":"pieces[3]"{{[^}]*}}"flags":0,{{[^}]*}}"count":1,{{[^}]*}}"excluded":false,"contributing":true,
// POINT-SAME: "name":"pieces[4]"{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false,
// POINT-SAME: "name":"pieces[5]"{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false,
// POINT-SAME: "name":"pieces[6]"{{[^}]*}}"flags":128,{{[^}]*}}"count":0,{{[^}]*}}"excluded":true,"contributing":false,
// POINT-SAME: "name":"pieces[7]"{{[^}]*}}"flags":0,{{[^}]*}}"count":2,{{[^}]*}}"excluded":false,"contributing":true,

// The automatic cross keeps only the three contributing bins times two side
// bins, so it covers 3 of 6 tuples.
// CROSS: "name":"product"
// CROSS-SAME: "covered":3,"total":6,"automatic_covered":3,"automatic_total":6,

// No illegal-bin diagnostics are produced for the empty partitions.
// DIAG: "illegal_bin_diagnostics":[]
