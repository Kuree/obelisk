// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.auto.out

// Two statements overlap on a wide root, and the loop executes one site
// repeatedly. The generated queue must retain every enqueue in source order.
module native_tier1_partial_wide_nba_ordered;
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
    #36;
    $display("%s low=%0d high=%0d edges=%0d loop_edges=%0d", message,
             wide[15:0], wide[127:64], edges, loop_edges);
    $finish;
  end
endmodule

// LLVM: @__obelisk_eval_ordered_nba_queue_v1 = internal global
// LLVM: call i32 @obelisk_rt_v1_eval_nba_reserve
// LLVM: define i32 @__obelisk_eval_dispatch_v1
// OUTPUT: ordered low=0 high=4 edges=4 loop_edges=4
