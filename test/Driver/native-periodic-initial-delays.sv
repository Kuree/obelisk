// RUN: talos -O3 --native-scheduler=eval %s -o %t.eval
// RUN: talos -O3 --native-scheduler=generic %s -o %t.generic
// RUN: env TALOS_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > %t.eval.out 2> %t.diag
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s < %t.eval.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

// IEEE 1800-2023 4.4-4.6, 9.4.1-9.4.2, 10.4.2: a persistent initial
// consumer must see pre-NBA state at the edge and post-NBA state at its exact
// positive delay. The driver must refresh deadlines after every resumed wait.
// More than 4096 edges rules out silently finishing in the bootstrap loop.
module native_periodic_initial_delays;
  logic clk;
  int ticks = 0;
  initial begin
    clk <= 0;
    forever begin
      #5 clk <= 0;
      #5 clk <= 1;
      ticks++;
    end
  end
  int q[8];
  for (genvar i = 0; i < 8; i++) begin
    initial q[i] = i;
    always @(posedge clk) q[i] <= q[i] + 1;
  end
  initial begin
    automatic string message = "periodic delays";
    for (int cycle = 0; cycle < 5000; cycle++) begin
      @(posedge clk);
      if (q[7] != cycle + 7) $fatal(1, "pre-NBA %0d %0d", cycle, q[7]);
      #2;
      if (q[7] != cycle + 8) $fatal(1, "delayed %0d %0d", cycle, q[7]);
      // Cross a falling edge; both waits belong to this same continuation.
      #5;
      if (clk !== 0) $fatal(1, "phase");
    end
    $display("%s %0d %0d at %0t", message, ticks, q[7], $time);
    $finish;
  end
endmodule

// CHECK: periodic delays 5000 5007 at 50007
// DIAG: aot_fallbacks=0
// DIAG-SAME: periodic_preparations=1 periodic_clocks_high_water=1 periodic_slot_drains={{[1-9][0-9]*}}
