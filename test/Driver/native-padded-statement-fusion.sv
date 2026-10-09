// RUN: obelisk -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.eval 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: %t.eval | FileCheck %s
// Each seven-bit element occupies a native byte. A fully assigned temporary
// has no retained input in that byte's padding. Its source snapshot must still
// survive slicing around the request/response cycle (IEEE 1800-2023 4.3, 4.6).
// PLAN: obelisk statement scheduling: eligible=2 cohorts=1 plans=1 dataflow=1
// CHECK: result=00,04
// CHECK-NEXT: result=1c,20
// CHECK-NEXT: result=7c,00
// CHECK-NEXT: result=xx,xx
// CHECK-NEXT: result=xx,xx
// CHECK-NEXT: result=04,08
module top;
  logic [6:0] seed;
  logic [6:0] request[2], response[2], temporary[2], result[2];
  always_comb begin
    for (int i = 0; i < 2; i++) begin
      temporary[i] = seed + i;
      result[i] = response[i] + temporary[i];
    end
    request = temporary;
  end
  always_comb begin
    for (int i = 0; i < 2; i++) response[i] = request[i] * 3;
  end
  initial begin
    seed = 0;
    #1 $display("result=%h,%h", result[0], result[1]);
    seed = 7;
    #1 $display("result=%h,%h", result[0], result[1]);
    seed = 31;
    #1 $display("result=%h,%h", result[0], result[1]);
    seed = 'x;
    #1 $display("result=%h,%h", result[0], result[1]);
    seed = 'z;
    #1 $display("result=%h,%h", result[0], result[1]);
    seed = 1;
    #1 $display("result=%h,%h", result[0], result[1]);
    $finish;
  end
endmodule
