// RUN: obelisk -emit-slang %s -o %t.slang.mlir
// RUN: obelisk -emit-obelisk %s -o %t.obelisk.mlir
module child #(parameter N = 3, type U = int);
endmodule
module parent #(parameter P = 2, type T = int);
  child #(7, byte) a();
  child #(.U(T), .N(P + 1)) b();
  child #(.N($clog2(P + 4)), .U()) c();
endmodule
module top;
  parent #(2, int) left();
  parent #(5, bit) right();
endmodule
