// RUN: obelisk -O3 --native-scheduler=eval --mlir-timing -emit-llvm %s -o %t.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.timing
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: %t.eval > %t.eval.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.eval.out

// IEEE 1800-2023 4.6(a), 9.4.2 and 11.5.1: preserve source order and X/Z;
// partial out-of-range writes affect only in-range bits, while wholly invalid
// or unknown selections have no effect. Zero checkpoints is compiler policy.
module native_tier1_dynamic_word_store;
  logic clk = 0;
  logic signed [31:0] low = 0;
  logic [3:0] payload = 4'b10xz;
  logic [31:0] result;
  bit [31:0] bits_result;
  always #5 clk = ~clk;
  always_comb begin
    result = 32'h12345678;
    result[low +: 4] = payload;
    bits_result = 32'h12345678;
    bits_result[low +: 4] = 4'ha;
  end
  initial begin
    #1; $display("%h %h", result, bits_result);
    low = -2;
    #1; $display("%h %h", result, bits_result);
    low = 30;
    #1; $display("%h %h", result, bits_result);
    low = 32;
    #1; $display("%h %h", result, bits_result);
    low = 'x;
    #1; $display("%h %h", result, bits_result);
    $finish;
  end
endmodule

// ADMISSION-NOT: reason=runtime-state-access
// ADMISSION: eval executor inventory: {{.*}}runtime_checkpoints=0
// LLVM-NOT: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// LLVM-NOT: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// OUTPUT: 1234567X 1234567a
// OUTPUT-NEXT: 1234567a 1234567a
// OUTPUT-NEXT: X2345678 92345678
// OUTPUT-NEXT: 12345678 12345678
// OUTPUT-NEXT: 12345678 12345678
