// RUN: not obelisk -emit-obelisk %s 2>&1 | FileCheck %s

module dpi_deprecated_unsupported;
  import "DPI" function int legacy_import(input int value);
endmodule

// CHECK: legacy SystemVerilog 3.1a `DPI` imports are unsupported; use `DPI-C`
