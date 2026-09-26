// RUN: not obelisk -O0 %s -o %t 2>&1 | FileCheck %s

// IEEE 1800-2017 10.6 excludes automatic variables, nonconstant net selects,
// and nets whose nettype is a user-defined nettype from force/release targets.
nettype logic user_net;

module force_general_illegal;
  logic index;
  logic [3:0] ordinary_variable;
  wire [3:0] ordinary_net;
  user_net custom_net;

  initial begin
    automatic logic automatic_value = 0;
    force automatic_value = 1;
    force ordinary_variable[0] = 1;
    force ordinary_net[index] = 1;
    force custom_net = 1;
  end
endmodule

// CHECK: cannot refer to automatic variable 'automatic_value' from non-procedural context
// CHECK: lvalue of force/release must be a net, a variable, a constant select of a net, or a concatenation of these
// CHECK: reference to non-constant variable 'index' is not allowed in a constant expression
// CHECK: force and release cannot be applied to nets with a user-defined nettype
