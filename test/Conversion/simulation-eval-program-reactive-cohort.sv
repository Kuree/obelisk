// RUN: talos -emit-slang %s | talos-opt --test-talos-native-backend='scheduler=eval timing=true' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s < %t.mlir

// IEEE 1800-2023 4.5 and 24.3: program processes run in the Reactive region.
// Generated eval admits the clocked datapath and the `result` net, but
// not the program's processes; their Re-NBAs to `b` and `c` stay ordered
// runtime commits. The runtime keeps generated Active ingress waiting for the
// whole Reactive/Re-NBA group (ProcessTest's
// Scheduler.ReactiveIterationSurvivesSingleStepReentry).
// unit_16 to unit_19 are the program's four initial processes.
// TRACE: ordered NBA boundary: function=unit_16 site=2 root=2
// TRACE-NEXT: ordered NBA boundary: function=unit_17 site=3 root=3
// TRACE: talos eval boundary: owner=0 island=0 body=unit_14 successors=
// TRACE-NEXT: talos eval dependency: owner=1 island=1 body=unit_10.__talos_eval_body_0
// TRACE-NEXT: talos eval dependency: owner=2 island=1 body=unit_4.__talos_eval_body_0
// TRACE-NEXT: talos eval dependency: owner=3 island=1 body=unit_6.__talos_eval_body_0
// TRACE-NEXT: talos eval dependency: owner=4 island=1 body=unit_8.__talos_eval_body_0
// TRACE-NOT: talos eval {{boundary|dependency}}
// CHECK: llvm.func @__talos_eval_dispatch_v1(

module tier1_reactive_top;
  logic clk = 0;
  integer cycles = 0;
  initial forever begin
    #5 clk <= 0;
    #5 clk <= 1;
    cycles += 1;
  end
  logic [7:0] q[4];
  for (genvar i = 0; i < 4; i++) begin
    initial q[i] = i;
    always @(posedge clk) q[i] <= q[i] + 1;
  end
  logic a = 0, b = 0, c = 0;
  wire [7:0] result = a ? q[3] : 8'd0;
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

