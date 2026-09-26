// RUN: obelisk -O0 --vpi=off --native-scheduler=generic %s -o %t
// RUN: %t --execution-tier=native | FileCheck %s
// RUN: %t --execution-tier=bytecode | FileCheck %s
// RUN: obelisk -O0 --vpi=off --native-scheduler=generic \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM \
// RUN:   --implicit-check-not='define {{.*}}.__member'

`ifndef SIZE
`define SIZE 32
`endif

primitive udp_cohort_scale_table(q, clock);
  output q;
  reg q;
  input clock;
  initial q = 0;
  table
    (x0) : ? : -;
    r    : 0 : 1;
    r    : 1 : 0;
    f    : ? : -;
  endtable
endprimitive

module sequential_udp_cohort_scale;
  reg [`SIZE-1:0] clock = 0;
  wire [`SIZE-1:0] q;
  integer cycle;

  udp_cohort_scale_table instances[`SIZE-1:0](q, clock);

  initial begin
    #1;
    for (cycle = 0; cycle < 100; cycle = cycle + 1) begin
      clock = ~clock;
      #1;
    end
    if (q !== {`SIZE{1'b0}})
      $fatal(0, "sequential UDP cohort scale mismatch");
    $display("SEQUENTIAL UDP COHORT SCALE PASS %0d", `SIZE);
    $finish;
  end
endmodule

// CHECK: SEQUENTIAL UDP COHORT SCALE PASS 32
// Thirty-two members require two bounded kernels but share one exact table
// evaluator across the chunk boundary.
// LLVM: define {{.*}}@__obelisk_region_kernel_{{.*}}.__member
