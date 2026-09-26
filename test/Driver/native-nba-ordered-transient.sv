// RUN: obelisk -O3 -fno-lto --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O3 -fno-lto --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// Two writes to each of the low slices leave the final value unchanged, but
// their intermediate edges must still wake the observers. The loop makes the
// root cost effective for generated scheduling and repeats one NBA site many
// times in a single activation.
module native_nba_ordered_transient;
  logic clk = 0;
  logic [31:0] wide = 0;
  int edges = 0;
  int loop_edges = 0;

  always #5 clk = ~clk;
  always @(posedge wide[0]) edges = edges + 1;
  always @(posedge wide[8]) loop_edges = loop_edges + 1;

  always @(posedge clk) begin
    wide[7:0] <= 8'h01;
    wide[7:0] <= 8'h00;
    for (int j = 0; j < 81; j = j + 1)
      wide[15:8] <= j & 1;
    wide[31:24] <= wide[31:24] + 1;
  end

  initial begin
    #36;
    $display("low=%0d high=%0d edges=%0d loop_edges=%0d",
             wide[15:0], wide[31:24], edges, loop_edges);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: low=0 high=4 edges=4 loop_edges=4
