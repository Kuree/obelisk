// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s \
// RUN:   -o %t.ll 2> %t.timing
// RUN: FileCheck %s --check-prefix=ADMISSION < %t.timing
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > %t.eval.out 2> %t.diag
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.eval.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

// IEEE 1800-2023 13.4: class constructors and ordinary methods do not
// suspend their caller. Heap access needs a native GC scope, not a bytecode
// activation. Keep generated eval ownership around that compiled callback.
module native_tier1_class_method;
  class item;
    int value = 3;
    function int get();
      return value;
    endfunction
  endclass
  item object;
  logic clk = 0;
  logic [31:0] result;
  logic [31:0] q[0:31];
  always #5 clk = ~clk;
  for (genvar i = 0; i < 32; i++) begin
    initial q[i] = i;
    always @(posedge clk) q[i] <= q[i] + 1;
  end
  always @(posedge clk) result <= object.get();
  initial begin
    object = new;
    #36;
    $display("%0d %0d", result, q[31]);
    $finish;
  end
endmodule

// ADMISSION: native eligibility: eligible=1 fully_eligible=1 cost_effective=1
// LLVM: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// OUTPUT: 3 35
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_fallbacks=0
// DIAG-SAME: eval_dispatches={{[1-9][0-9]*}}
