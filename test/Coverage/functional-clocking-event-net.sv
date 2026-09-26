// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov > %t.native.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module top;
  bit driver_a;
  bit driver_b;
  wand clock;
  bit sampled;

  assign clock = driver_a;
  assign clock = driver_b;

  covergroup clocked @(posedge clock);
    point: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  clocked coverage;
  int covered;
  int total;
  real percentage;

  initial begin
    coverage = new;

    // One driver changes, but the resolved wired-AND net remains zero. A
    // sampler watching the contribution handle would incorrectly fire here.
    sampled = 1;
    driver_a = 1;
    #0;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("resolved unchanged %.6f %0d %0d", percentage, covered, total);

    driver_b = 1;
    #0;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("resolved one %.6f %0d %0d", percentage, covered, total);

    sampled = 0;
    driver_a = 0;
    #0;
    driver_a = 1;
    #0;
    percentage = coverage.get_inst_coverage(covered, total);
    $display("resolved full %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// CHECK: resolved unchanged 0.000000 0 2
// CHECK-NEXT: resolved one 50.000000 1 2
// CHECK-NEXT: resolved full 100.000000 2 2
