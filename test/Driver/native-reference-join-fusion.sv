// RUN: obelisk -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.eval 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: %t.eval | FileCheck %s
// Merging two stores into a reference-phi block must not introduce a runtime
// checkpoint in the fused evaluator. Dynamic invalid/X selectors are no-ops.
// PLAN: obelisk statement scheduling: eligible=2 cohorts=1 plans=1 dataflow=1
// PLAN-NOT: checkpoint admission: {{.*}}function=__obelisk_region_kernel
// CHECK: result=03,00
// CHECK-NEXT: result=00,16
// CHECK-NEXT: result=00,00
// CHECK-NEXT: result=00,00
// CHECK-NEXT: result=xx,00
module top;
  typedef struct packed {logic valid; logic [3:0] ptr;} payload_t;
  payload_t left_side[1], right_side[1], temporary;
  logic [4:0] seed, request, response;
  logic choose;
  logic [1:0] index;
  always_comb begin
    temporary = response + 1;
    request = seed;
    left_side[0] = '0;
    right_side[0] = '0;
    if (choose) left_side[index] = temporary;
    else right_side[index] = temporary;
  end
  always_comb response = request ^ 5'h02;
  initial begin
    seed = 0; choose = 1; index = 0;
    #1 $display("result=%h,%h", left_side[0], right_side[0]);
    seed = 5'h17; choose = 0;
    #1 $display("result=%h,%h", left_side[0], right_side[0]);
    index = 1;
    #1 $display("result=%h,%h", left_side[0], right_side[0]);
    index = 'x;
    #1 $display("result=%h,%h", left_side[0], right_side[0]);
    index = 0; choose = 1; seed = 'x;
    #1 $display("result=%h,%h", left_side[0], right_side[0]);
    $finish;
  end
endmodule
