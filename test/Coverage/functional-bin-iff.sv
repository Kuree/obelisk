// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: not %t --coverage-output=%t.obcov --coverage-test=bin-iff 2>&1 | FileCheck %s --check-prefix=ERROR --implicit-check-not=ERROR:
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s
// RUN: not %t --coverage-load=%t.obcov --coverage-output=%t.loaded.obcov --coverage-test=bin-iff-loaded 2>/dev/null
// RUN: obelisk-cov report --format=json %t.loaded.obcov | FileCheck %s --check-prefix=JSON

module top;
  bit [2:0] sampled;
  bit point_on;
  bit ordinary_on;
  bit ignore_on;
  bit illegal_on;
  bit default_on;

  covergroup cg;
    cp: coverpoint sampled iff (point_on) {
      bins zero = {0} iff (ordinary_on);
      bins one_overlap = {1};
      bins two_overlap = {2};
      bins three = {3} iff (ordinary_on);
      ignore_bins ignored = {1} iff (ignore_on);
      illegal_bins illegal = {2} iff (illegal_on);
      illegal_bins illegal_overlap = {2} iff (illegal_on);
      bins fallback = default iff (default_on);
    }
  endgroup

  cg cov;
  initial begin
    cov = new;
    default_on = 1;

    // The coverpoint guard suppresses the complete sample.
    sampled = 0;
    ordinary_on = 1;
    cov.sample();

    // A false bin iff suppresses the bin increment but does not remove the
    // value from its defined set, so value 0 must not fall into fallback.
    point_on = 1;
    ordinary_on = 0;
    cov.sample();

    // Excluded membership removes overlapping ordinary values even when an
    // ignore/illegal guard is false. Illegal guards only gate diagnostics.
    sampled = 1;
    cov.sample();
    ignore_on = 1;
    cov.sample();

    sampled = 2;
    cov.sample();
    illegal_on = 1;
    cov.sample();

    // Exercise the coverpoint and ordinary bin guards in both states.
    sampled = 3;
    point_on = 0;
    ordinary_on = 1;
    cov.sample();
    point_on = 1;
    cov.sample();

    // The scalar default bin has its own independent sampling guard.
    sampled = 4;
    default_on = 0;
    cov.sample();
    default_on = 1;
    cov.sample();

    // A later hit of the same illegal bin is a distinct persisted diagnostic.
    #10;
    sampled = 2;
    illegal_on = 1;
    cov.sample();
    $finish;
  end
endmodule

// CHECK: functional: 50.00% (1/2)
// CHECK: coverpoint cp: 1/2 (50.00%)
// CHECK-DAG: bin zero: 0 [uncovered]
// CHECK-DAG: bin one_overlap: 0 [excluded] {empty}
// CHECK-DAG: bin two_overlap: 0 [excluded] {empty}
// CHECK-DAG: bin three: 1 [covered]
// CHECK-DAG: bin ignored: 0 [excluded] {ignore}
// CHECK-DAG: bin illegal: 0 [excluded] {illegal}
// CHECK-DAG: bin fallback: 1 [excluded] {default}

// Illegal guards only gate the diagnostics (19.5.6): each illegal bin reports
// exactly once per true-guard hit, at times 0 and 10, and nothing else is
// reported. Bins hit by one sample report in bin-ID order, which derives from
// the source path, so each line accepts either bin; the JSON records below
// pin one diagnostic per bin and time.
// ERROR: ERROR: functional coverage illegal bin 'top.cg.cp.{{illegal|illegal_overlap}}' sampled at simulation time 0
// ERROR-NEXT: ERROR: functional coverage illegal bin 'top.cg.cp.{{illegal|illegal_overlap}}' sampled at simulation time 0
// ERROR-NEXT: ERROR: functional coverage illegal bin 'top.cg.cp.{{illegal|illegal_overlap}}' sampled at simulation time 10
// ERROR-NEXT: ERROR: functional coverage illegal bin 'top.cg.cp.{{illegal|illegal_overlap}}' sampled at simulation time 10

// Loading the first run and running again persists a distinct diagnostic per
// run, test name and time: eight records, each with count 1.
// JSON-DAG: "test":"bin-iff","instance":{{[0-9]+}},"bin":{{[0-9]+}},"hierarchy":"top.cg.cp.illegal","simulation_time":0,"count":1,
// JSON-DAG: "test":"bin-iff","instance":{{[0-9]+}},"bin":{{[0-9]+}},"hierarchy":"top.cg.cp.illegal","simulation_time":10,"count":1,
// JSON-DAG: "test":"bin-iff","instance":{{[0-9]+}},"bin":{{[0-9]+}},"hierarchy":"top.cg.cp.illegal_overlap","simulation_time":0,"count":1,
// JSON-DAG: "test":"bin-iff","instance":{{[0-9]+}},"bin":{{[0-9]+}},"hierarchy":"top.cg.cp.illegal_overlap","simulation_time":10,"count":1,
// JSON-DAG: "test":"bin-iff-loaded","instance":{{[0-9]+}},"bin":{{[0-9]+}},"hierarchy":"top.cg.cp.illegal","simulation_time":0,"count":1,
// JSON-DAG: "test":"bin-iff-loaded","instance":{{[0-9]+}},"bin":{{[0-9]+}},"hierarchy":"top.cg.cp.illegal","simulation_time":10,"count":1,
// JSON-DAG: "test":"bin-iff-loaded","instance":{{[0-9]+}},"bin":{{[0-9]+}},"hierarchy":"top.cg.cp.illegal_overlap","simulation_time":0,"count":1,
// JSON-DAG: "test":"bin-iff-loaded","instance":{{[0-9]+}},"bin":{{[0-9]+}},"hierarchy":"top.cg.cp.illegal_overlap","simulation_time":10,"count":1,
// JSON-NOT: "hierarchy":"top.cg.cp.illegal
