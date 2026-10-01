// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: FileCheck %s --check-prefix=NO-CHECKPOINT < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// A private packed temporary keeps its activation lifetime (IEEE 1800-2023
// 6.21) through SSA promotion, including dynamic overlapping slices (11.5.1).
module native_tier1_automatic_packed_helper_checkpoint;
  logic clk = 0;
  logic [63:0] src = 1;
  logic [255:0] result;
  function automatic logic [255:0] make_line(input logic [63:0] data);
    logic [255:0] line;
    line = '0;
    for (int i = 0; i < 4; i++)
      line[i*64 +: 64] = data;
    return line;
  endfunction
  always #5 clk = ~clk;
  always @(posedge clk) src <= src + 1;
  always_comb result = make_line(src);
  initial begin
    #36;
    $display("%h", result);
    $finish;
  end
endmodule

// NO-CHECKPOINT-NOT: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// LLVM: call i32 @obelisk_rt_v1_native_state_bind_shared
// CHECK: 0000000000000005000000000000000500000000000000050000000000000005
