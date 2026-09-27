// RUN: obelisk -emit-sim -O0 --vpi=off %s | FileCheck %s

// Destination width determines the transfer size without manufacturing one
// operation per bit or word. A 4097-bit %z field is represented by one typed
// scan op; the runtime performs the 129-word decode loop.
module scan_binary_wide_lowering;
  string source;
  logic [4096:0] destination;
  integer status;

  initial status = $sscanf(source, "%z", destination);
endmodule

// CHECK-COUNT-1: simulation.string.scan_raw
// CHECK-SAME: four_state = true
// CHECK-SAME: (!simulation.string, i32) -> (!simulation.logic<4097>, i32, i32)
// CHECK-NOT: simulation.string.scan_raw
