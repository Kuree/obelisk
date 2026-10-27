// RUN: talos -O3 --native-scheduler=auto %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.native.out
// RUN: talos -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s -o %t.ll 2> %t.diag
// RUN: FileCheck %s --check-prefix=LLVM --implicit-check-not='call i32 @talos_rt_v1_scheduler_execute_aot_actor' --implicit-check-not='call i32 @talos_rt_v1_scheduler_packed_slice_nba' < %t.ll
// RUN: FileCheck %s --check-prefix=PROOF < %t.diag

// A bounded dynamic part-select in this repeated loop fits its parent.
// Auto keeps all overlapping writes in the generated ordered NBA queue,
// preserving every intermediate edge (IEEE 1800-2023 4.6(b), 10.4.2).
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

// LLVM: @__talos_eval_ordered_nba_queue_v1 = internal global
// LLVM: define {{.*}}i32 @__talos_eval_dispatch_v1
// PROOF-NOT: ordered NBA boundary:
// PROOF: talos fusion planning:
// PROOF-NOT: ordered NBA boundary:
// PROOF-NOT: partial eval disabled
// OUTPUT: ordered low=0 lanes=4f50 high=4 edges=4 loop_edges=4
