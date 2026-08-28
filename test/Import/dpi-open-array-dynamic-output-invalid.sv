// RUN: not obelisk -fno-lto %s -o %t 2>&1 | FileCheck %s

module dpi_open_array_dynamic_output_invalid;
  import "DPI-C" task invalid(output int values[]);
  int values[];

  initial invalid(values);
endmodule

// CHECK: dynamic-array and queue actuals cannot be passed to an output DPI open-array formal with unsized dimensions
