// RUN: obelisk -O0 %s -o %t.o0
// RUN: obelisk -O3 %s -o %t.o3
// RUN: %t.o0 > %t.o0.out && %t.o3 > %t.o3.out
// RUN: diff -u %t.o0.out %t.o3.out
// RUN: FileCheck %s < %t.o3.out

module native_partial_oob_handle;
  logic lower_guard;
  logic [3:0] value;
  logic upper_guard;
  int index;
  initial begin
    lower_guard = 1;
    value = 4'b0000;
    upper_guard = 1;
    index = -1;
    value[index +: 2] = 2'b10;
    index = 3;
    value[index +: 2] = 2'b01;
    $display("partial=%b guards=%b%b", value, lower_guard, upper_guard);
    value = 4'b0000;
    index = -1;
    value[index +: 2] <= 2'b10;
    index = 3;
    value[index +: 2] <= 2'b01;
    #1;
    $display("partial_nba=%b guards=%b%b", value, lower_guard, upper_guard);
  end
endmodule

// The lower selection maps only result bit 1 to value[0], and the upper
// selection maps only result bit 0 to value[3]. Adjacent roots remain intact.
// CHECK: partial=1001 guards=11
// CHECK: partial_nba=1001 guards=11
