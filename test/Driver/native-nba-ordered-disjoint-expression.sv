// RUN: obelisk -O3 -fno-lto --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O3 -fno-lto --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// The first NBA makes the whole-vector expression true; the following
// disjoint NBA makes it false. Neither bit is written twice, but combining
// the root's updates into one transition would lose the posedge (LRM 9.4.2).
module native_nba_ordered_disjoint_expression;
  logic clk = 0;
  logic [95:0] wide = 0;
  int edges = 0;

  always #5 clk = ~clk;
  always @(posedge (wide == 96'h1)) edges = edges + 1;
  always @(posedge clk) begin
    wide[31:0] <= 32'h1;
    wide[63:32] <= 32'h1;
    wide[95:64] <= 32'h1;
  end

  initial begin
    #6;
    $display("wide=%h edges=%0d", wide, edges);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: wide=000000010000000100000001 edges=1
