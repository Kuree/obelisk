// RUN: obelisk -emit-slang %s | obelisk-opt --test-obelisk-native-backend='scheduler=eval timing=true' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s --implicit-check-not='llvm.call @obelisk_rt_v1_scheduler_execute_aot_actor' < %t.mlir

// Two whole-root NBAs with a 128-bit payload write the same root in one
// activation. IEEE 1800-2023 4.6(b) and 10.4.2 perform both updates in order,
// so bit 0 and bit 100 each see a 0 -> 1 -> 0 pulse that the edge watchers
// count (9.4.2).
// The root keeps ordered commits: each site stages both 128-bit planes and
// reserves an entry in the generated ordered NBA queue, which the commit hands
// to the runtime's ordered entry point at the barrier. The datapath still runs
// through the generated evaluator, without a runtime actor checkpoint.
// TRACE: obelisk NBA root keeps ordered commits: root=0 width=128 semantic-sites=2
// CHECK-LABEL: llvm.func @unit_10.__obelisk_eval_body_0(
// CHECK: %[[QUEUE:.*]] = llvm.mlir.addressof @__obelisk_eval_ordered_nba_queue_v1
// CHECK: llvm.store %{{.*}}, %{{.*}} {alignment = 1 : i64} : i128, !llvm.ptr
// CHECK-NEXT: llvm.store %{{.*}}, %{{.*}} {alignment = 1 : i64} : i128, !llvm.ptr
// CHECK: llvm.call @obelisk_rt_v1_eval_nba_reserve(%{{.*}}, %[[QUEUE]])
// CHECK: llvm.store %{{.*}}, %{{.*}} {alignment = 1 : i64} : i128, !llvm.ptr
// CHECK-NEXT: llvm.store %{{.*}}, %{{.*}} {alignment = 1 : i64} : i128, !llvm.ptr
// CHECK: llvm.call @obelisk_rt_v1_eval_nba_reserve(%{{.*}}, %[[QUEUE]])
// CHECK-LABEL: llvm.func @__obelisk_eval_dispatch_v1(
// CHECK-LABEL: llvm.func internal @__obelisk_eval_ordered_nba_commit_v1(
// CHECK: llvm.call @obelisk_rt_v1_static_nba_commit_ordered(

module native_tier1_partial_wide_payload_nba_ordered;
  logic clk = 0;
  logic [127:0] wide = 0;
  int low_edges = 0;
  int high_edges = 0;
  logic [31:0] mix [0:2];

  always #5 clk = ~clk;
  always @(posedge wide[0]) low_edges = low_edges + 1;
  always @(posedge wide[100]) high_edges = high_edges + 1;
  assign mix[0] = wide[127:96] ^ wide[31:0];
  for (genvar g = 0; g < 2; g++) begin : chain
    assign mix[g + 1] = (mix[g] << 1) ^ (mix[g] >> 3) ^ (32'h9e3779b9 + g);
  end

  always @(posedge clk) begin
    wide <= (128'h1 << 100) | 128'h1;
    wide <= {wide[127:120] + 8'd1, 120'h0};
  end

  initial begin
    automatic string message;
    message = "ordered";
    #36;
    $display("%s top=%0d low_edges=%0d high_edges=%0d mix=%h", message,
             wide[127:120], low_edges, high_edges, mix[2]);
    $finish;
  end
endmodule

