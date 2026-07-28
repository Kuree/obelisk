// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_nba_order;
  logic [7:0] value;
  initial begin
    value = 0;
    value <= 8'h12;
    value[3:0] <= 4'hb;
    #1;
    $display("nba-order=%h", value);
  end
endmodule

// CHECK: nba-order=1b
