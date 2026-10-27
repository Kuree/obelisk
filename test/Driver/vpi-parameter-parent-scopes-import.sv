// RUN: talos %s -emit-slang -o %t.slang.mlir
// RUN: talos %s -emit-talos -o %t.talos.mlir
interface child #(parameter int N = 3, type U = int); endinterface
interface ip #(parameter int P = 11, type T = byte);
  child #(.N(P), .U(T)) a();
  child #(7, T) b();
endinterface
module top;
  ip i();
  ip #(13, bit) j();
endmodule
