// RUN: talos -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: talos -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.eval 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: %t.eval | FileCheck %s
// A fused cohort selects between its private array and external storage.
// Loads retain the value observed before the later private store, including
// X/Z planes. A local address choice must not force a runtime checkpoint.
// PLAN: talos statement scheduling: eligible=2 cohorts=1 plans=1 dataflow=1
// PLAN-NOT: checkpoint admission: {{.*}}__talos_region_kernel
// CHECK: result=1f,23
// CHECK-NEXT: result=08,0c
// CHECK-NEXT: result=xx,xx
// CHECK-NEXT: result=xx,xx
// CHECK-NEXT: result=13,17
module top;
  logic [5:0] seed;
  logic choose;
  logic [5:0] external_value[2], temporary[2];
  logic [5:0] request[2], response[2], result[2];
  always_comb begin
    for (int i = 0; i < 2; i++) temporary[i] = seed + i;
    result[0] = response[0] + temporary[0];
    result[1] = response[1] + temporary[1];
    request = choose ? temporary : external_value;
    temporary[0] = seed + 2;
  end
  always_comb begin
    for (int i = 0; i < 2; i++) response[i] = request[i] * 3;
  end
  initial begin
    seed = 1;
    choose = 0;
    external_value = '{10, 11};
    #1 $display("result=%h,%h", result[0], result[1]);
    seed = 2;
    choose = 1;
    #1 $display("result=%h,%h", result[0], result[1]);
    seed = 'x;
    #1 $display("result=%h,%h", result[0], result[1]);
    seed = 'z;
    #1 $display("result=%h,%h", result[0], result[1]);
    seed = 4;
    choose = 0;
    external_value = '{5, 6};
    #1 $display("result=%h,%h", result[0], result[1]);
    $finish;
  end
endmodule
