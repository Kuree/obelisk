// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=coverpoint-type-options > %t.native.stdout
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=coverpoint-type-options > %t.bytecode.stdout
// RUN: diff -u %t.native.stdout %t.bytecode.stdout
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.stdout
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt

module top;
  bit sampled;
  bit disabled;

  covergroup cg;
    type_option.merge_instances = 1;
    option.get_inst_coverage = 1;
    weighted: coverpoint sampled {
      type_option.weight = 3;
      type_option.goal = 50;
      bins zero = {0};
      bins one = {1};
    }
    missing: coverpoint sampled iff (disabled) {
      type_option.weight = 1;
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  cg cov;
  real percentage;
  int covered;
  int total;
  initial begin
    cov = new;
    sampled = 0;
    cov.sample();
    percentage = cov.get_inst_coverage(covered, total);
    $display("instance %.6f %0d %0d", percentage, covered, total);
    percentage = cg::get_coverage(covered, total);
    $display("type %.6f %0d %0d", percentage, covered, total);
    $display("global %.6f", $get_coverage());
    $finish;
  end
endmodule

// Instance options remain at defaults: (50% + 0%) / 2 = 25%. For the merged
// type, weighted's 50% reaches its type goal and contributes weight 3, while
// missing contributes 0 with weight 1: (3*100 + 1*0) / 4 = 75%.
// QUERY: instance 25.000000 1 4
// QUERY-NEXT: type 75.000000 3 4
// QUERY-NEXT: global 75.000000
// REPORT: functional: 75.00% (1/4)
// REPORT-DAG: type cg ({{[0-9]+}}): 75.00%
// REPORT-DAG: coverpoint weighted: 1/2 (50.00%)
