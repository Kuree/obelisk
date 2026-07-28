// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_oob_handle;
  logic [7:0] values [0:0];
  int index = 9;
  initial begin
    values[0] = 8'h5a;
    values[index][3] = 1;
    $display("value=%h", values[0]);
  end
endmodule

// CHECK: value=5a
