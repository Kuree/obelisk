// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module simulation_computed_wait;
  logic left;
  logic right;
  initial
    wait (left && right)
      left = 0;
endmodule

// CHECK: unsupported semantic node in the first simulation slice:
// CHECK-SAME: obelisk.sv.statement.wait
// CHECK-SAME: computed wait condition requires an observer
