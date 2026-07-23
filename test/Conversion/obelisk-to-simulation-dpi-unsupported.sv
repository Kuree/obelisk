// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module dpi_unsupported;
  import "DPI-C" function void consume(input string value);
endmodule

// CHECK: DPI imports support only scalar predefined integers
