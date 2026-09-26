// RUN: obelisk -O2 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O2 --native-scheduler=generic %s -o %t.generic
// RUN: %t.eval > %t.eval.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > /dev/null 2> %t.diag
// RUN: FileCheck %s --check-prefix=DIAG \
// RUN:   --implicit-check-not=obelisk-periodic-reject < %t.diag

module native_periodic_computed_timing_inventory;
  bit clock = 0;
  bit timing_data = 0, timing_reference = 0, timing_enable = 1;
  reg notifier = 0;
  int count = 0;

  always #1 clock = ~clock;
  always @(posedge clock)
    count <= count + 1;

  specify
    // IEEE 1800-2017 31.7 compiles this expression as a synchronous timing
    // evaluator. Immutable evaluator inventory is not a live observer wakeup
    // and must not disable the independent periodic AOT closure above.
    $setup(timing_data, posedge timing_reference &&&
           (timing_enable ^ 1'b0), 1, notifier);
  endspecify

  initial begin
    #10;
    $display("count=%0d notifier=%0d", count, notifier);
    $finish;
  end
endmodule

// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_node_executions={{[1-9][0-9]*}}
// DIAG-SAME: aot_fallbacks=0
