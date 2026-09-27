// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.auto > %t.out 2> %t.trace
// RUN: FileCheck %s < %t.out
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: %t.generic | FileCheck %s

module tier1_reactive_top;
  logic clk = 0;
  integer cycles = 0;
  initial forever begin
    #5 clk <= 0;
    #5 clk <= 1;
    cycles += 1;
  end
  logic [7:0] q[128];
  for (genvar i = 0; i < 128; i++) begin
    initial q[i] = i;
    always @(posedge clk) q[i] <= q[i] + 1;
  end
  logic a = 0, b = 0, c = 0;
  wire [7:0] result = a ? q[31] : 8'd0;
  tier1_reactive_program p();
endmodule

program tier1_reactive_program;
  real marker = 1.25;
  initial begin
    #12;
    tier1_reactive_top.a = 1;
    tier1_reactive_top.b <= 1;
    #1;
    $display("settled %0d", tier1_reactive_top.result);
    #12;
    $display("clock %0d", tier1_reactive_top.result);
  end
  initial begin
    @(posedge tier1_reactive_top.b);
    // IEEE 1800-2023 4.5: generated Active ingress must also wait for
    // the entire Reactive/Re-NBA group, across runtime checkpoints.
    $display("cohort %0d %f", tier1_reactive_top.result, marker);
    tier1_reactive_top.c <= 1;
  end
  initial begin
    @(posedge tier1_reactive_top.c);
    $display("chained %0d", tier1_reactive_top.result);
  end
  initial begin
    @(tier1_reactive_top.result);
    $display("generated %0d", tier1_reactive_top.result);
  end
endprogram

// CHECK: cohort 0 1.250000
// CHECK-NEXT: chained 0
// CHECK-NEXT: generated 32
// CHECK-NEXT: settled 32
// CHECK-NEXT: clock 33
// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// TRACE: eval_dispatches={{[1-9][0-9]*}}
