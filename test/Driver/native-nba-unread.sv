// RUN: obelisk --clocked-kernels=immediate -O3 --native-scheduler=eval --mlir-timing %s -o %t.eval 2> %t.plan
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > %t.eval.out 2> %t.diag
// RUN: FileCheck %s < %t.eval.out
// RUN: FileCheck %s --check-prefix=PLAN < %t.plan
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag
// RUN: obelisk --clocked-kernels=immediate -O3 --native-scheduler=eval --mlir-timing -DTRACE %s -o %t.trace 2> %t.trace.plan
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace.plan

// LRM 4.3: an entirely unread update may disappear. LRM 4.9.4: its
// RHS evaluation remains. Private q still needs its NBA for the next cycle.
module native_nba_unread;
`ifdef TRACE
  initial begin
    $dumpfile("unread.vcd");
    $dumpvars(0, native_nba_unread);
  end
`endif
  bit clk = 0;
  always #5 clk = ~clk;
  int unused_value;
  int effects = 0;
  int q = 0;
  function automatic int effectful_rhs();
    effects++;
    return effects;
  endfunction
  always @(posedge clk) begin
    unused_value <= effectful_rhs();
    q <= q + 1;
  end
  initial begin
    automatic string label = "unread NBA";
    for (int cycle=0; cycle<5000; cycle++) begin
      @(posedge clk);
      if (q != cycle) $fatal(1, "early update");
      #1;
      if (q != cycle+1 || effects != cycle+1) $fatal(1, "lost RHS or state");
    end
    $display("%s complete %0d %0d", label, q, effects);
    $finish;
  end
endmodule
// CHECK: unread NBA complete 5000 5000
// PLAN: obelisk native NBA batch:
// PLAN-SAME: unread=1
// DIAG: aot_nba_stages=5000 aot_nba_commits=5000
// TRACE: obelisk native NBA batch:
// TRACE-SAME: unread=0
