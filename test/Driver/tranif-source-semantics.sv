// RUN: obelisk -emit-slang %s | FileCheck %s

// Source-front-end acceptance only. Controlled-pass execution and lowering
// are covered from hand-authored simulation MLIR.
module tranif_source_semantics(
    input logic control,
    input logic [3:0] controls,
    inout wire [3:0] left,
    inout wire [3:0] right);
  tranif0 t0(left[0], right[0], control);
  tranif1 t1(left[1], right[1], control);
  rtranif0 rt0(left[2], right[2], control);
  rtranif1 rt1[0:0](left[3], right[3], control);
  tranif1 vector_controls[3:0](left, right, controls);
endmodule

// CHECK: slang.symbol.primitive_instance
// CHECK-SAME: primitive_name = "tranif0"
// CHECK: slang.symbol.primitive_instance
// CHECK-SAME: primitive_name = "tranif1"
// CHECK: slang.symbol.primitive_instance
// CHECK-SAME: primitive_name = "rtranif0"
// CHECK: slang.symbol.primitive_instance
// CHECK-SAME: primitive_name = "rtranif1"
// CHECK: slang.symbol.instance_array
// CHECK-SAME: name = "vector_controls"
// CHECK: slang.symbol.primitive_instance
// CHECK-SAME: primitive_name = "tranif1"
// CHECK: slang.expression.element_select
