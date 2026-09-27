// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s -o %t.ll 2> %t.diag
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: FileCheck %s --check-prefix=PROOF < %t.diag
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.auto.out

// A dynamic part-select in this repeated loop is a Tier-2 fragment. The
// generated queue cannot own every NBA site on the overlapping wide root,
// so Auto uses a runtime checkpoint for this activation while retaining
// Tier-1 for other actors (IEEE 1800-2023 4.6(b), 10.4.2).
module native_tier1_partial_wide_nba_dynamic_boundary;
  logic clk = 0;
  logic [127:0] wide = 0;
  int edges = 0;
  int loop_edges = 0;
  string message;

  always #5 clk = ~clk;
  always @(posedge wide[0]) edges = edges + 1;
  always @(posedge wide[8]) loop_edges = loop_edges + 1;

  always @(posedge clk) begin
    wide[7:0] <= 8'h01;
    wide[7:0] <= 8'h00;
    for (int j = 0; j < 81; j = j + 1)
      wide[15:8] <= j & 1;
    for (int k = 0; k < 81; k = k + 1)
      wide[16 + (k & 1) * 8 +: 8] <= k;
    wide[127:64] <= wide[127:64] + 1;
  end

  initial begin
    message = "ordered";
    #36;
    $display("%s low=%0d lanes=%0h high=%0d edges=%0d loop_edges=%0d",
             message, wide[15:0], wide[31:16], wide[127:64], edges,
             loop_edges);
    $finish;
  end
endmodule

// LLVM: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// PROOF: ordered NBA boundary:
// PROOF-NOT: partial eval disabled
// OUTPUT: ordered low=0 lanes=4f50 high=4 edges=4 loop_edges=4
