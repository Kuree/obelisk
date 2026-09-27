// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: FileCheck %s --check-prefix=CALENDAR < %t.ll
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.auto > %t.auto.out 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out

module checkpoint_clock_source(output logic clk = 0);
  integer cycles = 0;
  initial forever begin
    #5 clk <= 0;
    #5 clk <= 1;
    cycles += 1;
  end
endmodule

// The clock's NBA updates and side effect keep its calendar in the runtime.
// The port alias publishes to both generated logic and a runtime testbench.
// Preserve that event's runtime waiter delivery (IEEE 1800-2023 9.4.2) while
// the independent datapath remains in the generated eval closure.
module native_tier1_partial_runtime_nba_consumer;
  logic clk;
  checkpoint_clock_source source(clk);
  logic [7:0] q[128];
  logic pulse = 0;
  int pulses = 0;
  // NBA-generated edges must also reach a runtime-owned event consumer
  // (IEEE 1800-2023 9.4.2, 10.4.2). Losing them used to print pulses=0.
  always @(posedge clk) pulse <= ~pulse;
  real consumer = 2.0;
  initial forever begin
    @(posedge pulse);
    consumer += 1.0;
    pulses++;
  end
  real fraction = 1.25;
  string message;
  logic [255:0] stage1, stage2, unchanged;
  function automatic logic [255:0] make_line(input logic [63:0] data);
    logic [255:0] line;
    line = '0;
    for (int i = 0; i < 4; i++)
      line[i*64 +: 64] = data;
    return line;
  endfunction
  // The first cold activation publishes work for the second checkpoint.
  // Both must complete in this slot (IEEE 1800-2023 4.4-4.5, 9.4.2).
  always_comb stage1 = make_line(q[31]);
  always_comb stage2 = make_line(stage1[63:0] + 1);
  // After its first edge this callback publishes no change. It must not
  // strand other ready owners until a later clock edge (LRM 4.4-4.5).
  always @(posedge clk) unchanged = make_line(64'd7);
  for (genvar i = 0; i < 128; i++) begin : g
    initial q[i] = i;
    always @(posedge clk) q[i] <= q[i] + 1;
  end
  initial begin
    message = "clock port";
    repeat (16) @(posedge clk);
    $display("%f %s %0d", fraction, message, q[31]);
    #1;
    $display("%h", stage2);
    $display("%h", unchanged);
    $display("pulses=%0d consumer=%f", pulses, consumer);
    $finish;
  end
endmodule

// LLVM: call i32 @obelisk_rt_v1_scheduler_execute_aot_actor
// LLVM: define {{.*}}i32 @__obelisk_eval_dispatch_v1
// CALENDAR-NOT: @obelisk_rt_v1_scheduler_prepare_periodic_aot
// TRACE: eval_dispatches={{[1-9][0-9]*}}
