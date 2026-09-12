// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=coverpoint-query > %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=coverpoint-query > %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s < %t.native.txt

module top;
  bit [1:0] sampled;

  covergroup averaged(input int point_weight, input int point_goal);
    cp: coverpoint sampled {
      option.weight = point_weight;
      option.goal = point_goal;
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  covergroup merged(input int low, input int high,
                    input bit track_instance);
    type_option.merge_instances = 1;
    option.get_inst_coverage = track_instance;
    cp: coverpoint sampled {
      bins values[] = {[low:high]};
    }
  endgroup

  averaged low;
  averaged high;
  averaged weightless;
  merged cumulative;
  merged tracked;
  int covered;
  int total;
  real percentage;

  initial begin
    low = new(1, 100);
    high = new(3, 50);
    weightless = new(0, 100);
    sampled = 0;
    low.sample();
    sampled = 1;
    high.sample();

    percentage = low.cp.get_inst_coverage(covered, total);
    $display("low %.6f %0d %0d", percentage, covered, total);
    percentage = high.cp.get_inst_coverage(covered, total);
    $display("high %.6f %0d %0d", percentage, covered, total);
    percentage = weightless.cp.get_inst_coverage(covered, total);
    $display("weightless %.6f %0d %0d", percentage, covered, total);
    percentage = low.cp.get_coverage(covered, total);
    $display("instance type %.6f %0d %0d", percentage, covered, total);
    percentage = averaged::cp::get_coverage(covered, total);
    $display("static type %.6f %0d %0d", percentage, covered, total);

    cumulative = new(0, 1, 0);
    tracked = new(1, 2, 1);
    sampled = 0;
    cumulative.sample();
    sampled = 1;
    tracked.sample();
    percentage = cumulative.cp.get_inst_coverage(covered, total);
    $display("merged cumulative %.6f %0d %0d", percentage, covered, total);
    percentage = tracked.cp.get_inst_coverage(covered, total);
    $display("merged local %.6f %0d %0d", percentage, covered, total);
    percentage = merged::cp::get_coverage(covered, total);
    $display("merged type %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: low 50.000000 1 2
// CHECK-NEXT: high 100.000000 1 2
// CHECK-NEXT: weightless 0.000000 0 2
// CHECK-NEXT: instance type 87.500000 7 8
// CHECK-NEXT: static type 87.500000 7 8
// CHECK-NEXT: merged cumulative 66.666667 2 3
// CHECK-NEXT: merged local 50.000000 1 2
// CHECK-NEXT: merged type 66.666667 2 3
