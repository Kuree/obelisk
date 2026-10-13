// RUN: obelisk -emit-slang %s | obelisk-opt --test-obelisk-native-backend='scheduler=eval timing=true' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s < %t.mlir

// LRM 4.5, 9.4.2, 10.4.2: the clock's generated port copy must deliver runtime
// testbench waits before time advances. A blocking writer must also keep its
// own source event control inactive while notifying an independent runtime
// waiter of the same transition.
// unit_3 is the `trips` writer, eval owner 1 (actor 8). It publishes its
// blocking write under its own actor slot, so the runtime wakes the
// `@(trips)` waiter but not the writer's own `@(posedge clk or trips)`.
// unit_10 is the `clk` port copy, which publishes for the runtime waiters on
// `clk`.
// TRACE: obelisk eval owner: bit=1 actor=8 continuation=11
// TRACE: obelisk eval boundary: owner=0 island=0 body=unit_10.__obelisk_eval_body_0
// TRACE-NEXT: obelisk eval boundary: owner=1 island=0 body=unit_3.__obelisk_eval_body_0
// CHECK-LABEL: llvm.func @unit_3.__obelisk_eval_body_0(
// CHECK-DAG: %[[WIDTH:.*]] = llvm.mlir.constant(8 : i64) : i64
// CHECK-DAG: %[[ACTOR:.*]] = llvm.mlir.constant(8 : i32) : i32
// CHECK-DAG: %[[LOW:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition_owned(%{{.*}}, %[[ACTOR]], %{{.*}}, %[[LOW]], %[[WIDTH]],
// CHECK-LABEL: llvm.func @unit_10.__obelisk_eval_body_0(
// CHECK: llvm.call @obelisk_rt_v1_scheduler_static_transition_owned(

module publication_clock_source(output logic clk = 0, output integer cycles);
  initial begin
    clk <= 0;
    cycles = 0;
    forever begin
      #5 clk <= 0;
      #5 clk <= 1;
      cycles += 1;
    end
  end
endmodule

module native_tier1_runtime_publication;
  logic clk;
  integer cycles;
  publication_clock_source source(clk, cycles);
  logic [7:0] trips = 0;
  initial forever begin
    @(posedge clk or trips);
    if (clk) trips = trips + 1;
  end
  bit [7:0] q[4];
  for (genvar i = 0; i < 4; i++)
    always @(posedge clk) q[i] <= q[i] + 1;
  initial begin
    automatic string label = "observed";
    repeat (3) @(trips);
    $display("%s %0d", label, trips);
  end
  initial begin
    automatic string label = "final";
    repeat (4) @(negedge clk);
    $display("%s trips=%0d q=%0d cycles=%0d", label, trips, q[0], cycles);
    $finish;
  end
endmodule

