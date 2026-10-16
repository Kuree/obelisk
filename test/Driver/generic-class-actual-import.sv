// RUN: obelisk --std=1800-2023 -emit-slang %s -o %t.slang.mlir
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o %t.obelisk.mlir

// Acceptance only: retaining original inheritance syntax must not bind an
// unused dependent class or evaluate its actuals.
class Base #(type T = int, int P = 3, int Q = P + 1);
endclass
class Named #(type U, int N) extends Base #(.Q(), .P(N + 2), .T(U));
endclass
class Ordered #(int N) extends Base #(bit, N * 2);
endclass
class Unsupported #(type U, int N) extends Base #(U, $bits(U));
endclass
module top;
endmodule
