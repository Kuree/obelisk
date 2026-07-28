// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_unpacked_array_equality;
  bit left[1:0];
  bit right[1:0];
  logic unknown_left[0:0];
  logic unknown_right[0:0];

  initial begin
    left = '{1, 0};
    right = left;
    $display("same=%b,%b,%b,%b",
             left == right, left != right, left === right, left !== right);
    right[0] = 1;
    $display("different=%b,%b,%b,%b",
             left == right, left != right, left === right, left !== right);

    unknown_left[0] = 1'bx;
    unknown_right[0] = 1'bx;
    $display("unknown=%b,%b,%b,%b",
             unknown_left == unknown_right, unknown_left != unknown_right,
             unknown_left === unknown_right, unknown_left !== unknown_right);
  end
endmodule

// CHECK: same=1,0,1,0
// CHECK: different=0,1,0,1
// CHECK: unknown=x,x,1,0
