// RUN: obelisk --std=1800-2023 -emit-slang %s -o /dev/null
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o /dev/null
module parameterized #(type T = int, type U = T, type V = U);
  localparam type Local = T;
endmodule
module top;
  parameterized #(.T(logic [7:0])) left();
  parameterized #(.T(int), .U(bit)) right();
endmodule
