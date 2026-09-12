// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=coverpoint-options > %t.native.stdout
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: obelisk-cov report --format=json %t.native.obcov -o %t.native.json
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=coverpoint-options > %t.bytecode.stdout
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.stdout %t.bytecode.stdout
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.stdout
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt
// RUN: FileCheck %s --check-prefix=JSON < %t.native.json

module top;
  bit [3:0] tuned_value;
  bit baseline_value;
  bit disabled_value;
  bit disabled;

  covergroup cg(input int max_bins, input int minimum_hits,
                input int point_weight, input int point_goal);
    tuned: coverpoint tuned_value {
      option.auto_bin_max = max_bins;
      option.at_least = minimum_hits;
      option.weight = point_weight;
      option.goal = point_goal;
    }
    baseline: coverpoint baseline_value {
      bins zero = {0};
      bins one = {1};
    }
    no_hits: coverpoint disabled_value iff (disabled) {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg cov;
  initial begin
    cov = new(4, 2, 3, 50);
    tuned_value = 0;
    baseline_value = 0;
    cov.sample();
    tuned_value = 0;
    baseline_value = 1;
    cov.sample();
    tuned_value = 4;
    cov.sample();
    tuned_value = 8;
    cov.sample();
    tuned_value = 8;
    cov.sample();
    $display("coverage %.6f", cov.get_inst_coverage());
    $finish;
  end
endmodule

// The tuned point has two of four bins with at least two hits, reaches its
// 50% goal, and contributes with weight three. Baseline is fully covered and
// no_hits is uncovered, so the weighted group result is (3*100+100+0)/5.
// QUERY: coverage 80.000000
// REPORT: functional: 80.00% (4/8)
// REPORT-DAG: coverpoint tuned: 2/4 (100.00%)
// REPORT-DAG: coverpoint baseline: 2/2 (100.00%)
// REPORT-DAG: coverpoint no_hits: 0/2 (0.00%)
// REPORT-DAG: bin auto[0:3]: 2 [covered]
// REPORT-DAG: bin auto[4:7]: 1 [uncovered]
// REPORT-DAG: bin auto[8:11]: 2 [covered]
// JSON: "options":[
// JSON: "option":1,"value_kind":1,"value":50
// JSON: "option":2,"value_kind":1,"value":3
// JSON: "option":3,"value_kind":1,"value":2
// JSON: "option":4,"value_kind":1,"value":4
// JSON: "name":"tuned","comment":"","hierarchy":"top.cg.tuned","kind":1,"ordinal":0,"goal":50,"weight":3
// JSON: "name":"auto[0:3]","hierarchy":"top.cg.tuned.auto[0:3]","kind":1,"flags":64,"ordinal":0,"expansion_ordinal":0,"at_least":2
