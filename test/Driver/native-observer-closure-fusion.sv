// RUN: talos -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: talos -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.eval 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: %t.eval | FileCheck %s
// PLAN: talos statement scheduling: eligible=6 cohorts=1 plans=1 dataflow=1
// PLAN-NOT: checkpoint admission: {{.*}}function=__talos_region_kernel
// CHECK: result=01 first=01 second=02
// CHECK-NEXT: result=13 first=25 second=26
// CHECK-NEXT: result=xx first=xx second=xx
// CHECK-NEXT: result=00 first=ff second=00
module top;
  logic [7:0] seed, request, response, result, scratch;
  logic [7:0] bridge, first, scratch2, bridge2, second;
  always_comb begin
    result = response;
    request = seed;
    scratch = 0;
    scratch = seed;
  end
  always_comb response = request + 8'd1;
  always_comb begin
    bridge = result;
    scratch2 = 0;
    scratch2 = result;
  end
  always_comb first = bridge + scratch;
  always_comb bridge2 = bridge;
  always_comb second = bridge2 + scratch2;
  initial begin
    seed = 0;
    #1 $display("result=%h first=%h second=%h", result, first, second);
    seed = 8'h12;
    #1 $display("result=%h first=%h second=%h", result, first, second);
    seed = 'x;
    #1 $display("result=%h first=%h second=%h", result, first, second);
    seed = 8'hff;
    #1 $display("result=%h first=%h second=%h", result, first, second);
    $finish;
  end
endmodule
