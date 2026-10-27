// RUN: talos -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: talos -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.eval 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: %t.eval | FileCheck %s
// A conditional hold participates in the request/response cycle. Its old state
// and response publication must stay in one slice, including for X/Z controls.
// PLAN: talos statement scheduling: eligible=2 cohorts=1 plans=1 dataflow=1
// PLAN-NOT: checkpoint admission: {{.*}}function=__talos_region_kernel
// CHECK: result=xx
// CHECK-NEXT: result=10
// CHECK-NEXT: result=10
// CHECK-NEXT: result=ed
// CHECK-NEXT: result=ed
// CHECK-NEXT: result=xx
// CHECK-NEXT: result=xx
// CHECK-NEXT: result=08
module top;
  logic [7:0] address, request, response, result, held;
  logic [1:0] mode;
  always_comb begin
    result = response;
    request = address;
  end
  always_comb begin
    case (mode)
      2'b00: held = request;
      2'b01: held = ~request;
      2'b10: held = request << 1;
      2'b11: held = request >> 1;
    endcase
    response = held;
  end
  initial begin
    address = 0; mode = 'x;
    #1 $display("result=%h", result);
    address = 8'h10; mode = 0;
    #1 $display("result=%h", result);
    address = 8'h12; mode = 'x;
    #1 $display("result=%h", result);
    mode = 1;
    #1 $display("result=%h", result);
    address = 8'h22; mode = 'z;
    #1 $display("result=%h", result);
    address = 'x; mode = 0;
    #1 $display("result=%h", result);
    address = 8'h20; mode = 'z;
    #1 $display("result=%h", result);
    address = 8'h10; mode = 3;
    #1 $display("result=%h", result);
    $finish;
  end
endmodule
