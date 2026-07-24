// RUN: not obelisk -O0 %s -o %t 2>&1 | FileCheck %s

module unsupported_unpacked_selection;
  typedef struct {
    logic [31:0] elements [3:0];
  } aggregate_t;

  aggregate_t value;

  initial value.elements[3:1] = '{32'd4, 32'd3, 32'd2};
endmodule

// Unpacked aggregate ranges are retained as first-class values. Until slice
// assignment is implemented, reject this path before asking packed-width
// helpers to inspect a non-packed result type.
// CHECK: error: unsupported semantic node in the first simulation slice: obelisk.sv.expression.range_select (selection result type)
