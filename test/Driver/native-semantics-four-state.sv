// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_four_state;
  logic [3:0] value;
  initial begin
    value = 4'bzzzz;
    value <= 4'b10xz;
    #1;
    $display("four-state=%b", value);
  end
endmodule

// CHECK: four-state=10xz
