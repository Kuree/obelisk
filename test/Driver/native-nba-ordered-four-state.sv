// RUN: obelisk -O3 -fno-lto --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O3 -fno-lto --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// X separates update events. The edge and value-change observers must be
// scheduled as the generic scheduler schedules them, even when the final bit
// is 0. Obelisk performs every scheduled NBA before the processes they wake
// run, one of the orders IEEE 1800-2017 4.5 permits, so each observer wakes at
// most once per slot: the x -> 1 posedge after 0 -> x finds `edges` not yet
// waiting again.
module native_nba_ordered_four_state;
  logic clk = 0;
  logic [31:0] wide = 0;
  int edges = 0;
  int falls = 0;
  int z_edges = 0;
  int changes = 0;

  always #5 clk = ~clk;
  always @(posedge wide[0]) edges = edges + 1;
  always @(negedge wide[0]) falls = falls + 1;
  always @(posedge wide[1]) z_edges = z_edges + 1;
  always @(wide[0]) changes = changes + 1;

  always @(posedge clk) begin
    wide[0] <= 1'bx;
    wide[0] <= 1'b1;
    wide[0] <= 1'b0;
    wide[1] <= 1'bz;
    wide[1] <= 1'b0;
    for (int j = 0; j < 81; j = j + 1)
      wide[15:8] <= j & 1;
    wide[31:24] <= wide[31:24] + 1;
  end

  initial begin
    #36;
    $display("low=%b high=%0d edges=%0d falls=%0d z_edges=%0d changes=%0d",
             wide[1:0], wide[31:24], edges, falls, z_edges, changes);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: low=00 high=4 edges=4 falls=4 z_edges=4 changes=4
