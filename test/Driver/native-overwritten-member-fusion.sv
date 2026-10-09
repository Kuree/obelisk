// RUN: obelisk -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.eval 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: %t.eval | FileCheck %s
// PLAN: obelisk statement scheduling: eligible=3 cohorts=1 plans=1 dataflow=1
// PLAN-NOT: checkpoint admission: {{.*}}function=__obelisk_region_kernel
// CHECK: result=01
// CHECK-NEXT: result=13
// CHECK-NEXT: result=xx
// CHECK-NEXT: result=ff
module top;
  logic [7:0] seed, request, translated, response, scratch, result;
  always_comb begin
    result = response;
    request = seed;
  end
  always_comb translated = request + 8'd1;
  always_comb begin
    response = scratch;
    scratch = translated;
    response = translated;
  end
  initial begin
    seed = 0;
    #1 $display("result=%h", result);
    seed = 8'h12;
    #1 $display("result=%h", result);
    seed = 'x;
    #1 $display("result=%h", result);
    seed = 8'hfe;
    #1 $display("result=%h", result);
    $finish;
  end
endmodule
