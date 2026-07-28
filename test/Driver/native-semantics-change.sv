// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_change;
  logic value;
  initial begin
    #2;
    value = 1;
  end
  initial begin
    @(value);
    $display("changed=%0d", value);
  end
endmodule

// CHECK: changed=1
