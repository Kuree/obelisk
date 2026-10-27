// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: not %t --coverage-output=%t.obcov --coverage-test=cross-exclusions 2> %t.err | FileCheck %s --check-prefix=QUERY
// RUN: FileCheck %s --check-prefix=ERROR --implicit-check-not='illegal bin' < %t.err
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: talos-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: not %t --coverage-output=%t.zeta.obcov --coverage-test=zeta > /dev/null 2> /dev/null
// RUN: not %t --coverage-output=%t.alpha.obcov --coverage-test=alpha > /dev/null 2> /dev/null
// RUN: talos-cov report --format=text %t.zeta.obcov %t.alpha.obcov | FileCheck %s --check-prefix=ORDER

// IEEE 1800-2023 19.6.2-19.6.3 and 19.11.2: ignored and illegal products leave
// the denominator (ordinary plus the two automatic remainders), and only the
// b=2 sample reaches `ordinary`. Illegal wins over the overlapping ordinary
// bin and reports once although b=1 hits two overlapping target bins; the
// automatic remainder keeps one sparse tuple.
// QUERY: cross-exclusions 66.666667 2 3
// ERROR-DAG: ERROR: functional coverage illegal bin 'top.cg.product.illegal' sampled at simulation time 0
// ERROR-DAG: ERROR: functional coverage illegal bin 'top.array_cg.cp.bad[2]' sampled at simulation time 0
// JSON: "sparse_cross_tuples":[{{[{]}}"run":{{[^{]*}}"hits":1,{{[^{]*}}}],
// JSON: "hierarchy":"top.cg.product","kind":2,
// JSON-SAME: "covered":2,"total":3,"automatic_covered":1,"automatic_total":2,
// JSON-SAME: "name":"ordinary",{{[^}]*}}"count":1,"overflow":false,"excluded":false,
// JSON-SAME: "name":"ignored",{{[^}]*}}"flags":4,{{[^}]*}}"count":0,"overflow":false,"excluded":true,
// JSON-SAME: {{[{]}}"id":[[ILLEGAL:[0-9]+]],"template_bin":{{[0-9]+}},"name":"illegal",{{[^}]*}}"flags":8,{{[^}]*}}"excluded":true,
// JSON-SAME: "automatic_bins":[{{[{]}}"name":"<one,two>","count":1,
// JSON: "illegal_bin_diagnostics":[
// JSON-DAG: "bin":[[ILLEGAL]],"hierarchy":"top.cg.product.illegal","simulation_time":0,"count":1,
// JSON-DAG: "hierarchy":"top.array_cg.cp.bad[2]","simulation_time":0,"count":1,

// Resolved bin IDs are configuration-local, so the high=2 diagnostic names
// bad[2]. Merged diagnostics are ordered by test name, then bin.
// REPORT: illegal-bin diagnostics:
// REPORT-DAG: top.array_cg.cp.bad[2] instance 3 test "cross-exclusions": 1 at time 0 - "illegal bin sampled"
// REPORT-DAG: top.cg.product.illegal instance 1 test "cross-exclusions": 1 at time 0 - "illegal bin sampled"
// ORDER: illegal-bin diagnostics:
// ORDER-NEXT: top.array_cg.cp.bad[2] instance 3 test "alpha": 1 at time 0 - "illegal bin sampled"
// ORDER-NEXT: top.cg.product.illegal instance 1 test "alpha": 1 at time 0 - "illegal bin sampled"
// ORDER-NEXT: top.array_cg.cp.bad[2] instance 3 test "zeta": 1 at time 0 - "illegal bin sampled"
// ORDER-NEXT: top.cg.product.illegal instance 1 test "zeta": 1 at time 0 - "illegal bin sampled"

module top;
  bit [1:0] a;
  bit [1:0] b;
  bit ignore_on;
  bit illegal_on;

  covergroup cg;
    a_point: coverpoint a {
      bins zero = {0};
      bins one = {1};
      bins two = {2};
    }
    b_point: coverpoint b {
      bins zero = {0};
      bins one = {1};
      bins one_overlap = {1};
      bins two = {2};
    }
    product: cross a_point, b_point {
      bins ordinary = binsof(a_point.zero);
      ignore_bins ignored = binsof(b_point.zero) iff (ignore_on);
      illegal_bins illegal = binsof(b_point) intersect {1} iff (illegal_on);
    }
  endgroup

  cg cov;
  bit [1:0] array_value;
  covergroup array_cg(input int high);
    cp: coverpoint array_value {
      illegal_bins bad[] = {[1:high]};
    }
  endgroup
  array_cg low_config;
  array_cg high_config;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;
    low_config = new(1);
    high_config = new(2);

    // A false iff only stops the exclusion bin's own count: the ignored and
    // illegal products stay excluded from the overlapping ordinary bin, and
    // the disabled illegal bin reports nothing.
    a = 0;
    b = 0;
    cov.sample();
    b = 1;
    cov.sample();

    // 19.6.2: ignore wins over an overlapping ordinary bin.
    b = 0;
    ignore_on = 1;
    cov.sample();

    // 19.6.3: illegal wins over an overlapping ordinary bin and is retained
    // as one run-time diagnostic hit even though b=1 hits two overlapping
    // target bins selected by the illegal cross bin.
    b = 1;
    illegal_on = 1;
    cov.sample();

    // An ordinary-only selection increments its explicit bin.
    b = 2;
    cov.sample();

    // A tuple outside every user-defined selector increments the symbolic
    // automatic remainder.
    a = 1;
    cov.sample();

    // Resolved bin IDs are configuration-local.  A diagnostic from the
    // high=2 configuration must name bad[2], never bad[1] from high=1.
    array_value = 2;
    high_config.sample();

    percentage = cov.product.get_inst_coverage(covered, total);
    $display("cross-exclusions %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

