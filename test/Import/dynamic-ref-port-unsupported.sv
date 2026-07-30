// RUN: not obelisk -emit-obelisk %s 2>&1 | FileCheck %s

module unsupported_ref_child(ref logic value);
endmodule

module unsupported_dynamic_ref;
  logic [1:0] values;
  int index;
  unsupported_ref_child child(values[index]);
endmodule

// CHECK: invalid expression for pass by reference
