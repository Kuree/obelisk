// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=coverpoint-control > %t.native.txt
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=coverpoint-control > %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit [1:0] sampled;
  covergroup cg;
    cp_a: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
    cp_b: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
      bins two = {2};
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;

  initial begin
    cov = new;
    cov.cp_a.stop();
    sampled = 0;
    cov.sample();
    percentage = cov.cp_a.get_inst_coverage(covered, total);
    $display("a stopped %.6f %0d %0d", percentage, covered, total);
    percentage = cov.cp_b.get_inst_coverage(covered, total);
    $display("b active %.6f %0d %0d", percentage, covered, total);

    cov.cp_a.start();
    cov.cp_b.stop();
    sampled = 1;
    cov.sample();
    percentage = cov.cp_a.get_inst_coverage(covered, total);
    $display("a active %.6f %0d %0d", percentage, covered, total);
    percentage = cov.cp_b.get_inst_coverage(covered, total);
    $display("b stopped %.6f %0d %0d", percentage, covered, total);

    cov.stop();
    sampled = 0;
    cov.sample();
    cov.start();
    cov.sample();
    percentage = cov.cp_a.get_inst_coverage(covered, total);
    $display("group gate %.6f %0d %0d", percentage, covered, total);

    cov.cp_b.start();
    sampled = 2;
    cov.sample();
    percentage = cov.cp_a.get_inst_coverage(covered, total);
    $display("a final %.6f %0d %0d", percentage, covered, total);
    percentage = cov.cp_b.get_inst_coverage(covered, total);
    $display("b final %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: a stopped 0.000000 0 2
// CHECK-NEXT: b active 33.333333 1 3
// CHECK-NEXT: a active 50.000000 1 2
// CHECK-NEXT: b stopped 33.333333 1 3
// CHECK-NEXT: group gate 100.000000 2 2
// CHECK-NEXT: a final 100.000000 2 2
// CHECK-NEXT: b final 66.666667 2 3
