// RUN: obelisk -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --native-scheduler=eval %s -o %t.eval
// RUN: %t.eval | FileCheck %s
// Packed induction remains known even when the scanned data has X/Z. Its
// six-bit width must represent the final header value 32 without wrapping.
// CHECK: leading=32
// CHECK-NEXT: leading=0
// CHECK-NEXT: leading=15
// CHECK-NEXT: leading=32
// CHECK-NEXT: leading=16
module top;
  logic [31:0] data;
  logic [5:0] result;
  function automatic logic [5:0] leading(input logic [31:0] bits);
    logic [5:0] count;
    for (count = 0; count <= 31; count++)
      if (bits[31-count]) break;
    return count;
  endfunction
  always_comb result = leading(data);
  initial begin
    data = 0;
    #1 $display("leading=%0d", result);
    data = 32'h80000000;
    #1 $display("leading=%0d", result);
    data = 32'h00010000;
    #1 $display("leading=%0d", result);
    data = 32'hxxxxxxxx;
    #1 $display("leading=%0d", result);
    data = 32'hzzzz8000;
    #1 $display("leading=%0d", result);
    $finish;
  end
endmodule
