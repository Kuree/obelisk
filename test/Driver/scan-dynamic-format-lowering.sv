// RUN: obelisk -emit-sim -O0 --vpi=off %s | FileCheck %s

// Runtime-format dispatch is constant in the destination count and conversion
// families, never the destination width or format length. One destination and
// one finalizer therefore remain two interpreter calls at width 4096.
module scan_dynamic_format_lowering;
  string source;
  string format;
  logic [4095:0] destination;
  integer status;

  initial status = $sscanf(source, format, destination);
endmodule

// CHECK-COUNT-2: obelisk_sim.scan_dynamic_validate
// CHECK-COUNT-2: obelisk_sim.string.scan_dynamic
// CHECK-COUNT-2: obelisk_sim.string.parse_real
// CHECK-COUNT-4: obelisk_sim.string.parse_logic
// CHECK-NOT: obelisk_sim.string.scan_field
