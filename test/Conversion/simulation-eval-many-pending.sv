// RUN: talos -emit-slang %s | talos-opt --test-talos-native-backend='scheduler=eval timing=true' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s < %t.mlir

// LRM 4.6(b), 4.9.4 and 10.4.2: NBA captures keep their semantics when more
// than 64 roots share one activation. The 66 clocked processes share one clock
// cohort, so their pending updates span two readiness words. The testbench
// keeps a string across its waits and stays a runtime process.
// TRACE: talos ranked groups: candidates=66 groups=3 segments=1 members=66 clocked_members=66
// TRACE: talos eval local pending roots: 66 cores=6 fixed-candidates=66 bitmap-words=2 frame-bytes=1072
// The cohort's frame holds the two bitmap words and 66 value/unknown pairs.
// CHECK-LABEL: llvm.func @__talos_eval_ranked_group_0(
// CHECK: %[[WORDS:.*]] = llvm.mlir.constant(134 : i64) : i64
// CHECK-NEXT: %[[FRAME:.*]] = llvm.alloca %[[WORDS]] x i64
// CHECK: llvm.call @__talos_eval_ranked_group_0.__talos_pending(%arg0, %[[FRAME]])

module many_pending;
  bit clk = 0;
  always #5 clk = ~clk;
  for (genvar i = 0; i < 66; i++) begin : g
    int r;
    always @(posedge clk) r <= r + 1;
  end
  initial begin
    automatic string label = "done";
    repeat (3) @(posedge clk);
    #1 $display("%s %0d %0d", label, g[0].r, g[65].r);
    $finish;
  end
endmodule
