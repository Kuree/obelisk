// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s \
// RUN:   -o %t.auto.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.timing
// RUN: FileCheck %s --check-prefix=GENERIC < %t.auto.ll
// RUN: obelisk -O3 -fno-lto --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 -fno-lto --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out

// A single NBA site writes all 128 bits, exceeding the generated queue's
// 64-bit record payload. Auto retains the runtime evaluator for this case.
module native_tier1_partial_wide_nba_fallback;
  logic clk = 0;
  logic [7:0] q[0:31];
  logic [127:0] wide = 0;
  string message;

  always #5 clk = ~clk;
  genvar i;
  generate
    for (i = 0; i < 32; i = i + 1) begin : g
      initial q[i] = i;
      always @(posedge clk) q[i] <= q[i] + 1;
    end
  endgenerate

  always @(posedge clk) begin
    wide <= wide + 1;
    wide <= wide + 2;
  end

  initial begin
    message = "test";
    repeat (4) @(posedge clk);
    $display("%s %d %d", message, q[31], wide[63:0]);
    $finish;
  end
endmodule

// ADMISSION: native eligibility: eligible=1 fully_eligible=0 cost_effective=1
// GENERIC-NOT: @__obelisk_eval_dispatch_v1
