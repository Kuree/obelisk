// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s \
// RUN:   -o %t.auto.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.timing
// RUN: FileCheck %s --check-prefix=GENERATED < %t.auto.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out

// Two NBA statements each write all 128 bits of one root per activation.
// IEEE 1800-2023 4.6(b) and 10.4.2 perform both updates in order. The
// runtime calendar retains the complete payloads in its ordered queue. Auto
// keeps the generated evaluator with exact actor checkpoints and must match
// the generic scheduler, including the pre-NBA read at the final edge.
module native_tier1_partial_wide_nba_whole_root;
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
// ADMISSION-NOT: partial eval disabled
// GENERATED: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// GENERATED: define {{.*}}i32 @__obelisk_eval_dispatch_v1
