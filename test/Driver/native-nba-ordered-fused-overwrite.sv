// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.o0
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: %t.o0 > %t.o0.out
// RUN: %t.generic > %t.generic.out
// RUN: %t.auto > %t.auto.out
// RUN: diff -u %t.o0.out %t.generic.out
// RUN: diff -u %t.o0.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// `q <= 0; if (en) q <= 1;` performs two NBAs, so q goes 1 -> 0 -> 1 and the
// negedge observer wakes every cycle (IEEE 1800-2017 4.6(b), 9.4.2). Fusing
// the two clocked processes used to fold the pair into one stage of the final
// value, at every optimization level above -O0 and in every scheduler.
module native_nba_ordered_fused_overwrite;
  logic clk = 0;
  logic en = 1;
  logic q = 1;
  logic [31:0] count = 0;
  logic [31:0] other = 0;
  int falls = 0;

  always #5 clk = ~clk;
  always @(negedge q) falls = falls + 1;
  always @(posedge clk) begin
    q <= 0;
    if (en) q <= 1;
    count <= count + 1;
  end
  always @(posedge clk) other <= other + count;

  initial begin
    #36;
    $display("q=%b count=%0d other=%0d falls=%0d", q, count, other, falls);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: q=1 count=4 other=6 falls=4
