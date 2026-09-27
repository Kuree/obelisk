// RUN: obelisk -emit-sim -O0 --vpi=off %s | FileCheck %s

// A wide formatted-input destination is one exact-width parse operation. The
// runtime owns the O(input digits + destination words) power-of-two loop; no
// destination bit or word is expanded into generated IR.
module scan_numeric_wide_lowering;
  string source;
  logic [4095:0] destination;
  integer status;

  initial status = $sscanf(source, "%h", destination);
endmodule

// CHECK-COUNT-1: simulation.string.scan_field
// CHECK-COUNT-1: simulation.string.parse_logic
// CHECK-SAME: radix = <hex> : <4096>
// CHECK-NOT: simulation.string.parse_logic
