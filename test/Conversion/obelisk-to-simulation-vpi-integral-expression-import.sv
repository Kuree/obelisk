// RUN: %obelisk --std=1800-2023 -emit-obelisk %s -o %t
// RUN: FileCheck %s --input-file=%t
// CHECK: obelisk.sv.expression.conversion attributes {is_implicit = false

module top;
  bit [7:0] a, b;
  bit signed [7:0] negative;
  logic [7:0] condition;
  bit [7:0] sum, inverted, sized_cast;
  bit [15:0] widened, unsigned_context;
  bit less;
  logic [7:0] choice, quotient;

  initial sum = a + b;
  initial widened = (a + b) << 8'd1;
  initial unsigned_context = -negative + 16'd1;
  initial less = a < b;
  initial choice = condition ? a : b;
  initial inverted = ~a;
  initial quotient = a / b;
  initial sized_cast = 8'(a);
endmodule
