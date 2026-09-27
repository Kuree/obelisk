// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode.o3
// RUN: %t.bytecode.o3 | FileCheck %s

module reactive_cohort_top;
  logic a = 0, b = 0, c = 0;
  logic module_nba = 0;
  wire [7:0] result = a ? 8'd32 : 8'd0;
  always @(b) module_nba <= b;
  reactive_cohort_program p();
endmodule

program reactive_cohort_program;
  int wakes = 0;
  // Exercise ready-cache arbitration as well as the small exact scan.
  for (genvar i = 0; i < 24; i++) begin
    initial begin
      @(posedge reactive_cohort_top.b);
      if (reactive_cohort_top.result !== 0 ||
          reactive_cohort_top.module_nba !== 0)
        $fatal(1, "Active work interrupted the Reactive cohort");
      wakes++;
    end
  end
  initial begin
    #12;
    reactive_cohort_top.a = 1;
    reactive_cohort_top.b <= 1;
    // IEEE 1800-2023 4.5: Active work created by a program waits until
    // Reactive, Re-Inactive and Re-NBA all drain, including reactivations.
    #0;
    $display("re-inactive %0d %0d", reactive_cohort_top.result,
             reactive_cohort_top.module_nba);
    #1;
    if (wakes != 24) $fatal(1, "Reactive waiter was lost");
    $display("settled %0d %0d", reactive_cohort_top.result,
             reactive_cohort_top.module_nba);
    // Reenter the group in a later slot, then verify Active resumes again.
    reactive_cohort_top.a = 0;
    reactive_cohort_top.b <= 0;
    #0;
    $display("second %0d %0d", reactive_cohort_top.result,
             reactive_cohort_top.module_nba);
    #1;
    $display("settled-again %0d %0d", reactive_cohort_top.result,
             reactive_cohort_top.module_nba);
  end
  initial begin
    @(posedge reactive_cohort_top.b);
    $display("re-nba %0d %0d", reactive_cohort_top.result,
             reactive_cohort_top.module_nba);
    reactive_cohort_top.c <= 1;
  end
  initial begin
    @(posedge reactive_cohort_top.c);
    $display("chained %0d %0d", reactive_cohort_top.result,
             reactive_cohort_top.module_nba);
  end
endprogram

// CHECK: re-inactive 0 0
// CHECK-NEXT: re-nba 0 0
// CHECK-NEXT: chained 0 0
// CHECK-NEXT: settled 32 1
// CHECK-NEXT: second 32 1
// CHECK-NEXT: settled-again 0 0
