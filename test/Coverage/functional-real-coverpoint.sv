// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov --coverage-test=real-point 2>&1 | FileCheck %s --check-prefix=WARN --implicit-check-not='functional coverpoint'
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON --implicit-check-not='"option":11,'

// IEEE 1800-2023 19.5.1 and 19.7.1: real bins, open ranges, +/- and +%- ranges
// and real_interval, with detect_overlap warning only where it is enabled.
module top;
  real sampled;

  covergroup cg;
    type_option.real_interval = 0.25;
    option.detect_overlap = 1;
    cp: coverpoint sampled {
      type_option.real_interval = 0.1;
      option.detect_overlap = 0;
      bins exact = {1.5};
      bins interval = {[2.0:3.0]};
      bins absolute = {[4.0+/-0.5]};
      bins relative = {[5.0+%-10.0]};
      bins low = {[$:0.75]};
      bins high = {[1.25:$]};
      bins other = default;
    }
    overlap: coverpoint sampled {
      bins left = {[$:1.0]};
      bins right = {[0.5:$]};
    }
    quiet: coverpoint sampled {
      option.detect_overlap = 0;
      bins left = {[0.0:1.0]};
      bins right = {[0.5:2.0]};
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    sampled = -100.0;
    cov.sample();
    sampled = 0.75;
    cov.sample();
    sampled = 1.5;
    cov.sample();
    sampled = 2.5;
    cov.sample();
    sampled = 4.5;
    cov.sample();
    sampled = 5.5;
    cov.sample();
    sampled = 10.0;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 100.00% (10/10)
// CHECK-DAG: coverpoint cp: 6/6 (100.00%)
// CHECK-DAG: bin exact: 1 [covered]
// CHECK-DAG: bin interval: 1 [covered]
// CHECK-DAG: bin absolute: 1 [covered]
// CHECK-DAG: bin relative: 2 [covered]
// CHECK-DAG: bin low: 2 [covered]
// CHECK-DAG: bin high: 5 [covered]
// CHECK-DAG: bin other: 0 [excluded]
// CHECK-DAG: coverpoint overlap: 2/2 (100.00%)
// CHECK-DAG: coverpoint quiet: 2/2 (100.00%)

// Only the coverpoint with detect_overlap in effect warns about its
// overlapping real bins; cp and quiet disable it.
// WARN: warning: functional coverpoint 'top.cg.overlap' bins 'left' and 'right' overlap

// real_interval is stored as a real option (value_kind 5) holding the IEEE 754
// bit pattern: 0.1 on cp and 0.25 on the type, and no other real_interval.
// JSON-DAG: "option":11,"value_kind":5,"value":4598175219545276416,
// JSON-DAG: "option":11,"value_kind":5,"value":4591870180066957722,
