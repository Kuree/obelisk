// RUN: %talos --std=1800-2023 -O0 --coverage=line -o %t.line %s
// RUN: %t.line --coverage-output=%t.line.obcov --coverage-test=smoke
// RUN: talos-cov inspect %t.line.obcov | FileCheck %s --check-prefix=LINE-INSPECT
// RUN: talos-cov report --format=text %t.line.obcov | FileCheck %s --check-prefix=LINE
// RUN: %t.line --coverage-output=%t.suppressed.obcov --no-coverage-dump
// RUN: test ! -e %t.suppressed.obcov
// RUN: %talos --std=1800-2023 -O0 -o %t.functional-only %s
// RUN: %t.functional-only --coverage-output=%t.functional-only.obcov && test ! -e %t.functional-only.obcov

// Requesting line coverage dumps a database at exit with no further option,
// and --no-coverage-dump suppresses it. A design that uses covergroups but
// requests no coverage kind writes no database.
module top;
  bit value;
  covergroup cg;
    cp: coverpoint value {
      bins zero = {0};
    }
  endgroup
  cg c;
`ifdef REQUEST_COVERAGE_DATABASE
  real global_coverage;
`endif

  initial begin
    c = new;
    c.sample();
`ifdef REQUEST_COVERAGE_DATABASE
    global_coverage = $get_coverage();
`endif
`ifdef DUMP_FATAL
    $fatal(1, "expected simulation failure before coverage dump");
`else
    $finish;
`endif
  end
endmodule


// LINE-INSPECT: format: obcov
// LINE-INSPECT: runs: 1
// LINE-INSPECT: line-points: 3
// LINE-INSPECT: functional-types: 1
// LINE-INSPECT: functional-configurations: 0
// LINE-INSPECT: resolved-instances: 0
// LINE: line: 100.00% (3/3), partial 0
// LINE: functional: unavailable
