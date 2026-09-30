// RUN: obelisk -O3 --native-scheduler=eval --mlir-timing -emit-llvm %s \
// RUN:   -o %t.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=SLOTS < %t.ll
// RUN: FileCheck %s --check-prefix=NO-QUEUE < %t.ll
// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > %t.eval.out 2> %t.diag
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.eval.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

// The testbench retains a string across an event wait and reads before NBA.
// It shares the calendar with compiled RTL; it must not force independent
// wide clocked writes through actor checkpoints or an NBA event queue.
module native_tier1_shared_fixed_nba;
  logic clk = 0;
  logic [7:0] q[0:31];
  logic [127:0] wide = 0;
  logic [31:0] mem[0:7];
  logic [2:0] index = 0;
  always #5 clk = ~clk;
  for (genvar i = 0; i < 32; i++) begin
    initial q[i] = i;
    always @(posedge clk) q[i] <= q[i] + 1;
  end
  always @(posedge clk) begin
    wide <= wide + 128'h10000000000000001;
    mem[index] <= {24'h123456, q[0]};
    index <= index + 1;
  end
  initial begin
    automatic string message = "fixed";
    repeat (4) @(posedge clk);
    $display("%s pre %0d %0d %0d", message, q[31], wide[127:64], wide[63:0]);
    #1;
    $display("post %0d %0d %0d %h", q[31], wide[127:64], wide[63:0], mem[3]);
    $finish;
  end
endmodule

// SLOTS: @__obelisk_eval_nba_slots_
// SLOTS: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// NO-QUEUE-NOT: call {{.*}}@obelisk_rt_v1_eval_nba_reserve
// OUTPUT: fixed pre 34 3 3
// OUTPUT-NEXT: post 35 4 4 12345603
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_fallbacks=0
// DIAG-SAME: aot_checkpoints=0
