// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module simulation_computed_iff;
  logic clock;
  logic left;
  logic right;
  initial
    @(posedge clock iff (left && right))
      left = 0;
endmodule

// CHECK: unsupported semantic node in the first simulation slice:
// CHECK-SAME: obelisk.sv.timing.signal_event
// CHECK-SAME: computed iff condition
