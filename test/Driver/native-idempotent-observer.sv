// RUN: obelisk -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s
// RUN: obelisk -O3 --vpi=off --native-scheduler=eval %s -o %t.eval
// RUN: %t.eval | FileCheck %s
// A conditional hold observes a cyclic request/response pair. All four known
// modes assign it, while X/Z modes retain its previous value (12.5). Observer
// certification must preserve that state, including when it becomes X.
// CHECK: result=01 held=01
// CHECK-NEXT: result=11 held=01
// CHECK-NEXT: result=11 held=ee
// CHECK-NEXT: result=13 held=ee
// CHECK-NEXT: result=xx held=xx
// CHECK-NEXT: result=21 held=xx
// CHECK-NEXT: result=08 held=04
module top;
  logic [7:0] address, request, response, result, held;
  logic [1:0] mode;
  always_comb begin
    result = response;
    request = address;
  end
  always_comb response = request + 8'd1;
  always_comb begin
    case (mode)
      2'b00: held = result;
      2'b01: held = ~result;
      2'b10: held = result << 1;
      2'b11: held = result >> 1;
    endcase
  end
  initial begin
    address = 0; mode = 0;
    #1 $display("result=%h held=%h", result, held);
    address = 8'h10; mode = 2'bxx;
    #1 $display("result=%h held=%h", result, held);
    mode = 1;
    #1 $display("result=%h held=%h", result, held);
    address = 8'h12; mode = 2'bzz;
    #1 $display("result=%h held=%h", result, held);
    address = 8'hxx; mode = 0;
    #1 $display("result=%h held=%h", result, held);
    address = 8'h20; mode = 2'bzz;
    #1 $display("result=%h held=%h", result, held);
    address = 7; mode = 3;
    #1 $display("result=%h held=%h", result, held);
    $finish;
  end
endmodule
