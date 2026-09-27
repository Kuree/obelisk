// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.auto > %t.auto.out 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: sed -e 's/#4 clk/#5 clk/' -e 's/#6 clk/#5 clk/' %s > %t.periodic.sv
// RUN: obelisk -O3 --native-scheduler=auto %t.periodic.sv -o %t.periodic
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.periodic > %t.periodic.out 2> %t.periodic.trace
// RUN: diff -u %t.generic.out %t.periodic.out
// RUN: FileCheck %s --check-prefix=PERIODIC < %t.periodic.trace

module native_tier1_checkpoint_route_retention;
  logic clk = 0;
  integer cycles = 0;
  // Unequal phase delays retain the clockless wrapper under normalization.
  initial forever begin
    #4 clk <= 0;
    #6 clk <= 1;
    cycles += 1;
  end
  logic [7:0] q[128];
  logic [7:0] inverted[128];
  for (genvar i = 0; i < 128; i++) begin : g
    initial q[i] = i;
    always @(posedge clk) begin
      if (cycles == 4) q[i] <= 'x;
      else if (cycles == 6) q[i] <= 'z;
      else if (cycles == 8) q[i] <= i;
      else q[i] <= q[i] + 1;
    end
    always_comb inverted[i] = ~q[i];
  end
  logic [255:0] line;
  real fraction = 1.25;
  string message;
  function automatic logic [255:0] make_line(input logic [7:0] data);
    logic [255:0] result;
    for (int i = 0; i < 32; i++) result[i*8 +: 8] = data;
    return result;
  endfunction
  always_comb line = make_line(inverted[31]);
  // IEEE 1800-2023 6.8, 10.4.2: retaining a route across a checkpoint must
  // neither hide an NBA's X/Z update nor prevent recovery to known values.
  initial begin
    message = "runtime checkpoint";
    $display("%f %s", fraction, message);
    repeat (10) begin
      @(posedge clk);
      #1;
      $display("%0d %h %h %h", cycles, q[31], inverted[31], line[7:0]);
    end
    $finish;
  end
endmodule

// The hot wrapper must not reseed route pointers or pending proofs. The
// plan-installation invalidator remains responsible for fresh executions.
// LLVM-LABEL: define {{.*}}i32 @__obelisk_aot_schedule_run_v1(
// LLVM-NOT: @__obelisk_eval_function_route_v1_
// LLVM-NOT: @__obelisk_eval_route_promotion_pending_v1
// LLVM: call i32 @obelisk_rt_v1_scheduler_run_aot_nodes
// TRACE: eval_dispatches={{[1-9][0-9]*}}
// PERIODIC: periodic_preparations={{[1-9][0-9]*}}
// OUTPUT: 1 20 df df
// OUTPUT-NEXT: 2 21 de de
// OUTPUT-NEXT: 3 22 dd dd
// OUTPUT-NEXT: 4 xx xx xx
// OUTPUT-NEXT: 5 xx xx xx
// OUTPUT-NEXT: 6 zz xx xx
// OUTPUT-NEXT: 7 xx xx xx
// OUTPUT-NEXT: 8 1f e0 e0
// OUTPUT-NEXT: 9 20 df df
// OUTPUT-NEXT: 10 21 de de
