// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: FileCheck %s < %t.o0.native.out

module native_logic_equality;
  logic [7:0] left;
  logic [7:0] right;

  initial begin
    left = 8'b1101x001;
    right = 8'b1101x000;
    $display("known-mismatch eq=%b ne=%b", left == right, left != right);

    right = 8'b1101x001;
    $display("unresolved eq=%b ne=%b", left == right, left != right);

    left = 8'b11011001;
    right = 8'b11011001;
    $display("known-equal eq=%b ne=%b", left == right, left != right);
  end
endmodule

// CHECK: known-mismatch eq=0 ne=1
// CHECK-NEXT: unresolved eq=x ne=x
// CHECK-NEXT: known-equal eq=1 ne=0
