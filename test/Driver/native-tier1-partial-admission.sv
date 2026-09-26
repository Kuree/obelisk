// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s \
// RUN:   -o %t.auto.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.timing
// RUN: FileCheck %s --check-prefix=EVAL < %t.auto.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out

// An unrelated testbench string and real value exclude its actor. The clocked
// RTL still owns most of the graph and must retain its generated eval island.
module native_tier1_partial_admission;
  logic clk = 0;
  logic [7:0] q[0:31];
  real r = 1.25;
  string message;

  always #5 clk = ~clk;
  genvar i;
  generate
    for (i = 0; i < 32; i = i + 1) begin : g
      initial q[i] = i;
      always @(posedge clk) q[i] <= q[i] + 1;
    end
  endgenerate

  initial begin
    message = "test";
    repeat (16) @(posedge clk);
    $display("%f %s %d", r, message, q[31]);
    $finish;
  end
endmodule

// ADMISSION: native eligibility: eligible=1 fully_eligible=0 cost_effective=1
// EVAL: @__obelisk_direct_fragment_
// EVAL: @__obelisk_eval_dispatch_v1
