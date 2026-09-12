// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --target=native \
// RUN:   --native-scheduler=eval --coverage=functional --compile-threads=12 \
// RUN:   -o %t.aot %s
// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --native-scheduler=generic --coverage=functional \
// RUN:   --compile-threads=12 -o %t.generic %s
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.aot \
// RUN:   --coverage-output=%t.aot.obcov --coverage-test=strobe-aot \
// RUN:   > %t.aot.out 2> %t.aot.diag
// RUN: %t.generic --coverage-output=%t.generic.obcov \
// RUN:   --coverage-test=strobe-generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.aot.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.aot.out
// RUN: FileCheck %s --check-prefix=DIAG \
// RUN:   --implicit-check-not=obelisk-periodic-reject < %t.aot.diag
// RUN: obelisk-cov report --format=text %t.aot.obcov -o %t.report
// RUN: FileCheck %s --check-prefix=REPORT < %t.report

module functional_clocking_event_strobe_aot;
  bit clock = 0;
  bit phase = 0;
  bit gate = 1;
  bit sampled = 1;
  int edge_count;

  always #1 clock = ~clock;
  always @(posedge clock)
    edge_count <= edge_count + 1;

  // Exercise the eval scheduler's static transition bridge for a computed
  // primary with multiple dependencies. The iff input is read only when that
  // primary occurs and must not become an independent subscription root.
  covergroup clocked @(posedge (clock ^ phase) iff gate);
    type_option.strobe = 1;
    point: coverpoint sampled {
      bins one = {1};
    }
  endgroup

  clocked coverage = new;

  initial begin
    // Leave the runtime calendar idle through several generated periodic
    // edges. Each posedge queues runtime-owned Postponed work, which must be
    // drained before the generated calendar can advance to the next edge.
    #10;
    $display("strobe periodic done edges=%0d", edge_count);
    $finish;
  end
endmodule

// QUERY: strobe periodic done edges=5
// REPORT: functional: 100.00% (1/1)
// DIAG: obelisk-signal-diagnostics
// DIAG-SAME: aot_node_executions={{[1-9][0-9]*}}
// DIAG-SAME: aot_fallbacks=0
