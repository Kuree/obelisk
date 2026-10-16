// RUN: obelisk -emit-slang %s -o - | FileCheck %s
// RUN: obelisk -emit-obelisk %s -o - | FileCheck %s
module top;
  logic [7:0] ones, xs;
  bit [7:0] to_two;
  initial ones = '1;
  initial xs = 'x;
  initial to_two = 'x;
endmodule
// CHECK: expression.unbased_unsized_integer_literal attributes {constant_value = "8'd255"
// CHECK-SAME: semantic_type = !{{(slang\.packed_array|obelisk\.ranged_packed_array)}}<7 : 0
// CHECK: expression.unbased_unsized_integer_literal attributes {constant_value = "8'bxxxxxxxx"
// CHECK-SAME: semantic_type = !{{(slang\.packed_array|obelisk\.ranged_packed_array)}}<7 : 0
// CHECK: expression.conversion attributes {folded_constant = "8'd0", is_implicit = true
// CHECK: expression.unbased_unsized_integer_literal attributes {constant_value = "8'bxxxxxxxx"
