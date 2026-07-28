// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_unpacked_array_assignment;
  int descending[3:0];
  int ascending[0:3];

  initial begin
    descending[0] = 0;
    descending[1] = 1;
    descending[2] = 2;
    descending[3] = 3;
    ascending = descending;
    $display("ascending=%0d,%0d,%0d,%0d",
             ascending[0], ascending[1], ascending[2], ascending[3]);
  end
endmodule

// Assignment pairs elements by ordinal position, not by source index.
// CHECK: ascending=3,2,1,0
