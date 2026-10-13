// RUN: obelisk -emit-slang %s | obelisk-opt --test-obelisk-native-backend='scheduler=eval' -o %t.mlir
// RUN: FileCheck %s --implicit-check-not='llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot' < %t.mlir

// Generated eval admits the datapath of a design whose clock keeps a runtime
// calendar.
// The unequal phase delays keep the clock's calendar in the runtime, so no
// periodic plan is prepared. The real-valued testbench actors stay runtime
// processes that wait on `clk` and `pulse` (IEEE 1800-2023 9.4.2).
// The port alias `source.clk` -> `clk` runs in the generated closure. On a
// transition it wakes the generated `posedge clk` owners through the ingress
// word and hands the same transition, with its old and new planes, to the
// runtime so the testbench's waiters are delivered.
// CHECK-LABEL: llvm.func @unit_16.__obelisk_eval_body_0(
// CHECK: %[[INGRESS:.*]] = llvm.mlir.addressof @__obelisk_aot_model_ingress_v1
// CHECK: %[[QUEUED:.*]] = llvm.load %[[INGRESS]]
// CHECK-NEXT: %[[WOKEN:.*]] = llvm.or %[[QUEUED]], %{{.*}} : i64
// CHECK-NEXT: llvm.store %[[WOKEN]], %[[INGRESS]]
// CHECK-NEXT: llvm.call @obelisk_rt_v1_scheduler_static_transition_owned(
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// An NBA-generated edge of `pulse` must reach its runtime waiter as well
// (IEEE 1800-2023 9.4.2, 10.4.2): the commit goes through the runtime's
// ordered NBA entry point rather than a silent store.
// CHECK-LABEL: llvm.func @__obelisk_aot_static_nba_commit_v1(
// CHECK: llvm.call @obelisk_rt_v1_static_nba_commit_ordered(

module checkpoint_clock_source(output logic clk = 0);
  initial forever begin
    #4 clk <= 0;
    #6 clk <= 1;
  end
endmodule

module top;
  logic clk;
  checkpoint_clock_source source(clk);
  logic [7:0] q[4];
  logic pulse = 0;
  int pulses = 0;
  always @(posedge clk) pulse <= ~pulse;
  real consumer = 2.0;
  initial forever begin
    @(posedge pulse);
    consumer += 1.0;
    pulses++;
  end
  for (genvar i = 0; i < 4; i++) begin : g
    initial q[i] = i;
    always @(posedge clk) q[i] <= q[i] + 1;
  end
  initial begin
    repeat (16) @(posedge clk);
    $display("%0d %0d %f", q[3], pulses, consumer);
    $finish;
  end
endmodule
