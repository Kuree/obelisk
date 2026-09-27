// RUN: obelisk -O3 --vpi=off -D AUTO_NMOS %s -o %t.auto
// RUN: obelisk -O0 --vpi=off --native-scheduler=generic -G N=32 %s -o %t.generic-o0
// RUN: obelisk -O3 --vpi=off --native-scheduler=aot -G N=32 %s -o %t.aot-o3
// RUN: obelisk -O3 --vpi=off --native-scheduler=eval -G N=32 %s -o %t.eval-o3
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic-o0 > %t.generic-o0.out
// RUN: %t.aot-o3 > %t.aot-o3.out
// RUN: %t.eval-o3 > %t.eval-o3.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: %t.bytecode-o3 > %t.bytecode-o3.out
// RUN: diff -u %t.auto.out %t.generic-o0.out
// RUN: diff -u %t.auto.out %t.aot-o3.out
// RUN: diff -u %t.auto.out %t.eval-o3.out
// RUN: diff -u %t.auto.out %t.bytecode-o0.out
// RUN: diff -u %t.auto.out %t.bytecode-o3.out
// RUN: FileCheck %s --implicit-check-not="LARGE PRIMITIVE FAIL" < %t.auto.out
// RUN: obelisk -O0 --vpi=off --execution-tier=native -emit-sim %s -o - | FileCheck %s --check-prefix=KERNEL
// RUN: obelisk -O3 --vpi=off -D AUTO_NMOS -emit-sim %s -o - | FileCheck %s --check-prefix=AUTO

// A large cohort of scalar built-in primitives stays compact under the
// default native auto policy. The executable selects bytecode execution
// instead of materializing one LLVM coroutine per instance.
module primitive_large_cohort;
  parameter int N = 128;
  logic [N-1:0] data;
  logic [N-1:0] control;
  wire [N-1:0] result;
  bit clock = 0;
  int ticks;

`ifndef AUTO_NMOS
  always #1 clock = ~clock;
  always @(posedge clock) ++ticks;
`endif

  genvar i;
  generate
    for (i = 0; i < N; ++i)
`ifdef AUTO_NMOS
      nmos n0(result[i], data[i], control[i]);
`else
      and a0(result[i], data[i], control[i]);
`endif
  endgenerate

  initial begin
    data = '0;
    control = '1;
    #1 data[7] = 1'b1;
    #1;
    if (result !== data)
      $display("LARGE PRIMITIVE FAIL");
    else
      $display("LARGE PRIMITIVE PASS");
    $finish;
  end
endmodule

// CHECK: LARGE PRIMITIVE PASS
// KERNEL-COUNT-8: obelisk_sim.func private @__obelisk_region_kernel_
// KERNEL-NOT: schedule.primitive_name = "and"
// The acyclic graph keeps the input ranges in Tier-1. The dynamically changed
// lane has distinct ownership from the two unchanged ranges.
// AUTO: #schedule.scheduled_root<resource = storage, descriptor = 0, low = 0, width = 7, owner = {{[0-9]+}}, tier = tier1>
// AUTO-SAME: #schedule.scheduled_root<resource = storage, descriptor = 0, low = 7, width = 1, owner = {{[0-9]+}}, tier = tier1>
// AUTO-SAME: #schedule.scheduled_root<resource = storage, descriptor = 0, low = 8, width = 120, owner = {{[0-9]+}}, tier = tier1>
