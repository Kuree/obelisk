// RUN: talos -O3 %s -o %t.hybrid
// RUN: %t.hybrid | FileCheck %s
// RUN: talos --no-two-state-specialization -O3 %s -o %t.four
// RUN: %t.four | FileCheck %s
// RUN: talos --no-two-state-specialization --execution-tier=bytecode -O3 %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: talos --no-two-state-specialization -emit-slang %s | FileCheck %s --check-prefix=FLAG
// RUN: talos --no-two-state-specialization -O3 -emit-llvm %s | FileCheck %s --check-prefix=LLVM --implicit-check-not='__talos_two_state' --implicit-check-not='__talos_known' --implicit-check-not='__talos_eval_kernel_promotion_scan'

// FLAG: native.disable_two_state_specialization
// FLAG: !slang.integral<1, false, true,
// LLVM: define
// CHECK: four-state semantics passed
module top;
  logic clk = 0;
  logic [31:0] value, next_value, output_value;
  bit known;
  always #1 clk = ~clk;
  always_comb next_value = (value + 1) ^ (value >> 3);
  always @(posedge clk) output_value <= next_value;
  initial begin
    if (!$isunknown(value) || known !== 0) $fatal(1, "initial state");
    value = 41;
    #2;
    if (output_value !== ((32'd41 + 1) ^ (32'd41 >> 3)))
      $fatal(1, "known computation");
    value = 'x;
    #2;
    if (!$isunknown(output_value)) $fatal(1, "X propagation");
    value = 'z;
    #2;
    if (!$isunknown(output_value)) $fatal(1, "Z propagation");
    value = 7;
    #2;
    if (output_value !== 8) $fatal(1, "recovery");
    $display("four-state semantics passed");
    $finish;
  end
endmodule
