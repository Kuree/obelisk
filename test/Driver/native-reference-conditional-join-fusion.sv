// RUN: talos -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: talos -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.eval 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: %t.eval | FileCheck %s
// A case can merge selected array reads into a reference-phi block ending in
// a conditional branch. Each chosen read must keep its own native root.
// PLAN: talos statement scheduling: eligible=2 cohorts=1 plans=1 dataflow=1
// PLAN-NOT: checkpoint admission: {{.*}}function=__talos_region_kernel
// CHECK: next=11
// CHECK-NEXT: next=10
// CHECK-NEXT: next=01
// CHECK-NEXT: next=10
// CHECK-NEXT: next=xx
module top;
  logic req[1], done[1], release_req[1];
  logic [1:0] phase, next_phase[1], seed, request, response, temporary;
  always_comb begin
    temporary = response;
    request = seed;
    next_phase[0] = phase;
    for (int i = 0; i < 1; ++i)
      case (phase)
        0: if (req[i]) next_phase[i] = temporary;
        1: if (done[i]) next_phase[i] = temporary;
        2: if (release_req[i]) next_phase[i] = temporary;
        default: ;
      endcase
  end
  always_comb response = request ^ 2'b11;
  initial begin
    phase = 0; seed = 0;
    req[0] = 1; done[0] = 1; release_req[0] = 1;
    #1 $display("next=%b", next_phase[0]);
    phase = 1; seed = 1;
    #1 $display("next=%b", next_phase[0]);
    phase = 2; seed = 2;
    #1 $display("next=%b", next_phase[0]);
    release_req[0] = 'x;
    #1 $display("next=%b", next_phase[0]);
    phase = 'x;
    #1 $display("next=%b", next_phase[0]);
    $finish;
  end
endmodule
