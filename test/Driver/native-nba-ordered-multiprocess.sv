// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// Separate processes stage the same root in one slot. The #0 places the
// second writer in Inactive, after the first writer executes in Active and
// before NBA updates begin. This gives their NBAs a defined order.
module native_nba_ordered_multiprocess;
  logic clk = 0;
  logic [31:0] wide = 0;
  int edges = 0;

  always #5 clk = ~clk;
  always @(posedge wide[0]) edges = edges + 1;

  always @(posedge clk) wide[0] <= 1'b1;
  always @(posedge clk) begin
    #0;
    wide[0] <= 1'b0;
  end
  always @(posedge clk) begin
    for (int j = 0; j < 81; j = j + 1)
      wide[15:8] <= j & 1;
    wide[31:24] <= wide[31:24] + 1;
  end

  initial begin
    #36;
    $display("low=%b high=%0d edges=%0d", wide[0], wide[31:24], edges);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: low=0 high=4 edges=4
