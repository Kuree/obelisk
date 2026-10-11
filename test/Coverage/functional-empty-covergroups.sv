// RUN: %obelisk --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: %t +load_only --coverage-load=%t.obcov --coverage-output=%t.loaded.obcov | FileCheck %s --check-prefix=LOAD
// RUN: obelisk-cov report --format=text %t.loaded.obcov | FileCheck %s --check-prefix=REPORT
// RUN: obelisk-cov inspect %t.loaded.obcov | FileCheck %s --check-prefix=INSPECT
// RUN: %obelisk --std=1800-2023 -DEMPTY_ONLY -O0 --coverage=functional -o %t.empty %s
// RUN: %t.empty --coverage-output=%t.empty.obcov | FileCheck %s --check-prefix=EMPTY
// RUN: obelisk-cov report --format=text %t.empty.obcov | FileCheck %s --check-prefix=EMPTY-REPORT

// Covergroups without coverage items are valid, independent instances
// (IEEE 1800-2023 19.3). With no bins, 19.11 scores an instance or type 0%,
// or 100% when its weight is 0; they contribute nothing to the functional
// total, which stays available with a 0/0 denominator.

module top;
  bit value;
  covergroup plain;
  endgroup
  covergroup weighted(input int weight, input string name)
      with function sample(input int unused);
    option.per_instance = 1;
    option.weight = weight;
    option.name = name;
  endgroup
  covergroup weightless_type;
    option.per_instance = 1;
    type_option.weight = 0;
  endgroup
  covergroup merged;
    option.per_instance = 1;
    option.get_inst_coverage = 1;
    type_option.merge_instances = 1;
    type_option.weight = 0;
  endgroup
`ifndef EMPTY_ONLY
  covergroup anchor;
    option.per_instance = 1;
    cp: coverpoint value { bins zero = {0}; bins one = {1}; }
  endgroup
`endif
  plain first, second;
  weighted positive, zero;
  weightless_type z;
  merged m;
`ifndef EMPTY_ONLY
  anchor a;
`endif
  int covered, total;
  real percentage;
  initial begin
    if ($test$plusargs("load_only")) begin
      percentage = plain::get_coverage(covered, total);
      $display("LOADED plain %.0f %0d %0d", percentage, covered, total);
      percentage = weightless_type::get_coverage(covered, total);
      $display("LOADED weightless %.0f %0d %0d", percentage, covered, total);
      percentage = merged::get_coverage(covered, total);
      $display("LOADED merged %.0f %0d %0d", percentage, covered, total);
      $display("LOADED global %.0f", $get_coverage());
      $finish;
    end
    percentage = plain::get_coverage(covered, total);
    $display("BEFORE %.0f %0d %0d %.0f", percentage, covered, total, $get_coverage());
    first = new;
    second = new;
    positive = new(3, "positive-empty");
    zero = new(0, "zero-empty");
    z = new;
    m = new;
    if (!first || !second || first == second)
      $fatal(1, "empty covergroups need independent nonnull identities");
    first.stop();
    first.sample();
    first.start();
    first.sample();
    positive.sample(17);
    zero.sample(23);
    positive.set_inst_name("renamed-empty");
    percentage = positive.get_inst_coverage(covered, total);
    $display("POSITIVE %.0f %0d %0d", percentage, covered, total);
    percentage = zero.get_inst_coverage(covered, total);
    $display("ZERO %.0f %0d %0d", percentage, covered, total);
    percentage = weightless_type::get_coverage(covered, total);
    $display("TYPE ZERO %.0f %0d %0d", percentage, covered, total);
    percentage = z.get_inst_coverage(covered, total);
    $display("INSTANCE POSITIVE %.0f %0d %0d", percentage, covered, total);
    z.option.weight = 0;
    percentage = z.get_inst_coverage(covered, total);
    $display("INSTANCE ZERO %.0f %0d %0d", percentage, covered, total);
    percentage = merged::get_coverage(covered, total);
    $display("MERGED ZERO %.0f %0d %0d", percentage, covered, total);
    $display("EMPTY GLOBAL %.0f", $get_coverage());
`ifndef EMPTY_ONLY
    a = new;
    value = 0;
    a.sample();
    value = 1;
    a.sample();
    percentage = anchor::get_coverage(covered, total);
    $display("ANCHOR %.0f %0d %0d", percentage, covered, total);
`endif
    $display("GLOBAL %.0f", $get_coverage());
    $finish;
  end
endmodule

// CHECK: BEFORE 0 0 0 100
// CHECK-NEXT: POSITIVE 0 0 0
// CHECK-NEXT: ZERO 100 0 0
// CHECK-NEXT: TYPE ZERO 100 0 0
// CHECK-NEXT: INSTANCE POSITIVE 0 0 0
// CHECK-NEXT: INSTANCE ZERO 100 0 0
// CHECK-NEXT: MERGED ZERO 100 0 0
// CHECK-NEXT: EMPTY GLOBAL 100
// CHECK-NEXT: ANCHOR 100 2 2
// CHECK-NEXT: GLOBAL 100
// EMPTY: BEFORE 0 0 0 100
// EMPTY: ZERO 100 0 0
// EMPTY: TYPE ZERO 100 0 0
// EMPTY: MERGED ZERO 100 0 0
// EMPTY: EMPTY GLOBAL 100
// EMPTY-NEXT: GLOBAL 100
// LOAD: LOADED plain 0 0 0
// LOAD-NEXT: LOADED weightless 100 0 0
// LOAD-NEXT: LOADED merged 100 0 0
// LOAD-NEXT: LOADED global 100

// Saving and reloading keeps the empty configurations and their options.
// REPORT: tests: 2
// REPORT: functional: 100.00% (2/2)
// REPORT-DAG: type plain ({{[0-9]+}}): 0.00%
// REPORT-DAG: type weighted ({{[0-9]+}}): 0.00%
// REPORT-DAG: type weightless_type ({{[0-9]+}}): 100.00%
// REPORT-DAG: type merged ({{[0-9]+}}): 100.00%
// REPORT-DAG: type options: goal 100, weight 0, merge_instances true
// REPORT-DAG: type options: goal 100, weight 0, merge_instances false
// REPORT-DAG: instance renamed-empty: 0.00% (0/0) [per_instance=true]
// REPORT-DAG: instance zero-empty: 100.00% (0/0) [per_instance=true]
// REPORT-DAG: coverpoint cp: 2/2 (100.00%)
// INSPECT: runs: 2
// INSPECT: functional-types: 5
// INSPECT: functional-configurations: 6
// INSPECT: resolved-functional-items: 1
// INSPECT: resolved-functional-bins: 2
// INSPECT: resolved-instances: 7

// A design whose covergroups declare no bins at all keeps its functional
// metadata.
// EMPTY-REPORT: functional: 100.00% (0/0)
// EMPTY-REPORT-DAG: type plain ({{[0-9]+}}): 0.00%
// EMPTY-REPORT-DAG: type weighted ({{[0-9]+}}): 0.00%
// EMPTY-REPORT-DAG: type weightless_type ({{[0-9]+}}): 100.00%
// EMPTY-REPORT-DAG: type merged ({{[0-9]+}}): 100.00%
// EMPTY-REPORT-DAG: instance zero-empty: 100.00% (0/0) [per_instance=true]
