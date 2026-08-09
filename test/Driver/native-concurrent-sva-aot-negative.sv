// RUN: not obelisk --std=1800-2023 -O3 --native-scheduler=aot %s -o %t 2>&1 | FileCheck %s

module native_concurrent_sva_aot_negative;
  logic clk = 0, a = 1;
  default clocking cb @(posedge clk); endclocking

  // The detached Reactive report is an allowed cold hybrid boundary.
  cover property (a) $display("covered");

  // An unrelated user task remains outside the narrowly admitted boundary.
  task automatic delayed_task;
    #1;
  endtask
  initial delayed_task();
endmodule

// CHECK: error: design is ineligible for native AOT scheduling:
// CHECK-SAME: task, await, or join control is present
