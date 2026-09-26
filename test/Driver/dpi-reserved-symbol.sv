// RUN: not obelisk %s -o %t 2>&1 | FileCheck %s

module dpi_reserved_symbol;
  import "DPI-C" obelisk_rt_v1_import_call = function int bad(input int value);
  int value;
  initial value = bad(1);
endmodule

// CHECK: DPI C identifier 'obelisk_rt_v1_import_call' collides with a reserved generated/runtime symbol
