// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module simulation_event_selection;
  logic [7:0] values;
  int index;
  initial
    @(values[index])
      values = '0;
endmodule

// CHECK: unsupported semantic node in the first simulation slice:
// CHECK-SAME: obelisk.sv.timing.signal_event
// CHECK-SAME: computed edge expression
