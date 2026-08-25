// RUN: not obelisk -emit-sim -O0 --vpi=off %s 2>&1 | FileCheck %s

module scan_binary_suppression_invalid;
  string source;
  integer status;

  initial status = $sscanf(source, "%*u");
endmodule

// CHECK: assignment suppression for raw %u requires an explicit byte count
