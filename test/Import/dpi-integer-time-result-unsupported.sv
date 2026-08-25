// RUN: not obelisk -emit-obelisk %s 2>&1 | FileCheck %s

module dpi_integer_time_result_unsupported;
  // IEEE 1800-2017 35.5.5 permits integer and time as formal argument types,
  // but 35.5.4 does not include either among imported function result types.
  import "DPI-C" function integer invalid_integer_result();
  import "DPI-C" function time invalid_time_result();
endmodule

// CHECK-DAG: 'integer' is not a valid return type for a DPI subroutine
// CHECK-DAG: 'time' is not a valid return type for a DPI subroutine
