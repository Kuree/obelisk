// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module dynamic_array_dpi_unsupported;
  import "DPI-C" function void consume(input int value[]);
endmodule

// CHECK: DPI-C dynamic-array, queue, and associative-array marshalling is unsupported
