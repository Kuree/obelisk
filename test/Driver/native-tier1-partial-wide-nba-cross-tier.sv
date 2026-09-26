// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s -o %t.ll 2> %t.diag
// RUN: FileCheck %s --check-prefix=GENERIC < %t.ll
// RUN: FileCheck %s --check-prefix=PROOF < %t.diag
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out

// The excluded testbench also enqueues to the overlapping root. Its writes
// cannot be interleaved with the generated queue, so Auto keeps runtime NBA
// ownership for this root.
module native_tier1_partial_wide_nba_cross_tier;
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
    wide[127:64] <= wide[127:64] + 1;
  end

  initial begin
    message = "ordered";
    wide[7:0] <= 8'h55;
    #36;
    $display("%s low=%0d high=%0d edges=%0d loop_edges=%0d", message,
             wide[15:0], wide[127:64], edges, loop_edges);
    $finish;
  end
endmodule

// GENERIC-NOT: @__obelisk_eval_dispatch_v1
// PROOF: partial eval disabled: runtime-ordered NBA owner
