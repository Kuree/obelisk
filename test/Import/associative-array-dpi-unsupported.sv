// RUN: not obelisk -emit-obelisk %s 2>&1 | FileCheck %s

module associative_array_dpi_unsupported;
  import "DPI-C" function void consume(input int value[string]);
endmodule

// CHECK: is not a valid argument type in a DPI subroutine
