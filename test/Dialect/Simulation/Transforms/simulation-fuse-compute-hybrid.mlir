// RUN: %split-file %s %t
// RUN: obelisk --native-scheduler=auto -emit-llvm %t/hybrid.sv -o %t/hybrid.ll
// RUN: FileCheck %s --check-prefix=HYBRID < %t/hybrid.ll

// HYBRID-NOT: @__obelisk_aot_schedule_plan_v1
// HYBRID: call i64 @obelisk_rt_v1_process_spawn
// HYBRID-NOT: call i32 @obelisk_rt_v1_scheduler_install_aot
// HYBRID: call i32 @obelisk_rt_v1_scheduler_run

//--- hybrid.sv
module hybrid_fusion;
  bit clock;
  bit count;
  string first;
  string second;

  always @(posedge clock)
    first = "first";

  always @(posedge clock)
    second = "second";

  always @(posedge clock)
    count = ~count;

  initial begin
    #1 clock = 1;
    #1;
    $display("%s %s %0d", first, second, count);
    $finish;
  end
endmodule
