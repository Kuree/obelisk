// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s -o %t.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.auto.out

// Two whole-root NBAs with a 128-bit payload write the same root in one
// activation. IEEE 1800-2023 4.6(b) and 10.4.2 perform both updates in order,
// so bit 0 and bit 100 each see a 0 -> 1 -> 0 pulse that the edge watchers
// count (9.4.2). The generated queue stages each payload as two 64-bit
// records; bits 0 and 100 lie in different records, and every bit still sees
// both of its updates in execution order.
module native_tier1_partial_wide_payload_nba_ordered;
  logic clk = 0;
  logic [127:0] wide = 0;
  int low_edges = 0;
  int high_edges = 0;
  logic [31:0] mix [0:63];
  string message;

  always #5 clk = ~clk;
  always @(posedge wide[0]) low_edges = low_edges + 1;
  always @(posedge wide[100]) high_edges = high_edges + 1;
  // Enough native work for Auto to select the partial island.
  assign mix[0] = wide[127:96] ^ wide[31:0];
  for (genvar g = 0; g < 63; g++) begin : chain
    assign mix[g + 1] = (mix[g] << 1) ^ (mix[g] >> 3) ^ (32'h9e3779b9 + g);
  end

  always @(posedge clk) begin
    wide <= (128'h1 << 100) | 128'h1;
    wide <= {wide[127:120] + 8'd1, 120'h0};
  end

  initial begin
    message = "ordered";
    #36;
    $display("%s top=%0d low_edges=%0d high_edges=%0d mix=%h", message,
             wide[127:120], low_edges, high_edges, mix[63]);
    $finish;
  end
endmodule

// TIER-NOT: partial eval disabled
// LLVM: @__obelisk_eval_ordered_nba_queue_v1 = internal global
// LLVM: call i32 @obelisk_rt_v1_eval_nba_reserve
// LLVM: define i32 @__obelisk_eval_dispatch_v1
// OUTPUT: ordered top=4 low_edges=4 high_edges=4 mix=
