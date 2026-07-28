// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_nba;
  logic [7:0] value;
  initial begin
    value = 1;
    value <= 2;
    $display("active=%0d", value);
    #1;
    $display("after=%0d", value);
  end
  final $display("final=%0d", value);
endmodule

// CHECK: active=1
// CHECK-NEXT: after=2
// CHECK-NEXT: final=2
