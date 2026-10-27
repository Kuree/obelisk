// RUN: talos -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s -o %t.ll 2> %t.diag
// RUN: FileCheck %s --check-prefix=PROOF < %t.diag
// RUN: FileCheck %s --check-prefix=IR --implicit-check-not='call i32 @talos_rt_v1_scheduler_execute_aot_actor' --implicit-check-not='call i32 @talos_rt_v1_scheduler_packed_slice_nba' < %t.ll
// RUN: talos -O3 --native-scheduler=auto %s -o %t.auto
// RUN: %t.auto | FileCheck %s --check-prefix=OUTPUT

// The generated ordered queue owns repeated writes, including a bounded
// dynamic packed slice. IEEE 1800-2023 4.6(b), 9.4.2 and 10.4.2: preserve
// the intermediate edges as well as the final value without a checkpoint.
module native_partial_auto_fallback_ordered_nba;
  logic clk = 0;
  logic [127:0] wide = 0;
  int low_edges = 0;
  int upper_edges = 0;
  string label;

  always #5 clk = ~clk;
  always @(posedge wide[0]) low_edges++;
  always @(posedge wide[64]) upper_edges++;

  always @(posedge clk) begin
    wide[7:0] <= 8'h01;
    wide[7:0] <= 8'h00;
    for (int j = 0; j < 81; j++)
      wide[15:8] <= j & 1;
    for (int k = 0; k < 81; k++)
      wide[16 + (k & 1) * 8 +: 8] <= k;
    wide[127:64] <= wide[127:64] + 1;
  end

  initial begin
    label = "ordered";
    #36;
    $display("%s low=%h high=%0d low_edges=%0d upper_edges=%0d",
             label, wide[31:0], wide[127:64], low_edges, upper_edges);
    $finish;
  end
endmodule

// PROOF-NOT: ordered NBA boundary:
// PROOF: talos fusion planning:
// PROOF-NOT: ordered NBA boundary:
// PROOF-NOT: partial eval disabled
// IR: @__talos_eval_ordered_nba_queue_v1 = internal global
// IR: define {{.*}}i32 @__talos_eval_dispatch_v1
// OUTPUT: ordered low=4f500000 high=4 low_edges=4 upper_edges=2
