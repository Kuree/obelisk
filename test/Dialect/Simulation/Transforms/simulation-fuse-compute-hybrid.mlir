// RUN: %split-file %s %t
// RUN: obelisk --native-scheduler=auto -emit-llvm %t/hybrid.sv -o %t/hybrid.ll
// RUN: FileCheck %s --check-prefix=HYBRID < %t/hybrid.ll
// RUN: obelisk --native-scheduler=auto %t/hybrid.sv -o %t/hybrid
// RUN: %t/hybrid 2>&1 | FileCheck %s --check-prefix=OUT

// HYBRID: @__obelisk_aot_schedule_plan_v1
// HYBRID: call i64 @obelisk_rt_v1_process_spawn
// HYBRID: call i32 @obelisk_rt_v1_scheduler_install_aot
// LRM 9.4.2 and 4.6: all three clocked assignments complete before #1 display.
// OUT: first second 1

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
