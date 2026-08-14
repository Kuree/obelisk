// RUN: obelisk -fno-lto --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.generic
// RUN: obelisk -fno-lto --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: obelisk -fno-lto --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.aot
// RUN: %t.generic > %t.generic.out 2>&1
// RUN: %t.bytecode > %t.bytecode.out 2>&1
// RUN: %t.aot > %t.aot.out 2>&1
// RUN: diff -u %t.generic.out %t.bytecode.out
// RUN: diff -u %t.generic.out %t.aot.out
// RUN: FileCheck %s < %t.aot.out
// RUN: obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=SIM
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot -emit-llvm %s | FileCheck %s --check-prefix=AOT --implicit-check-not=obelisk_rt_v1_scheduler_run_aot_nodes

module native_concurrent_sva_observable;
  logic clk = 0, alternate_clk = 0, fail_clk = 0;
  logic a = 0, b = 0, c = 0, never = 0, always_false = 0;
  logic assert_pass = 0, assert_fail = 0;
  logic assume_pass = 0, assume_fail = 0;
  logic cover_delay = 0, cover_repeat = 0, cover_explicit_1 = 0;
  logic cover_explicit_2 = 0, cover_explicit_3 = 0, cover_explicit_4 = 0;
  logic cover_vacuous = 0;
  int report_order = 0;
  default clocking cb @(posedge clk); endclocking

  assert property (a) assert_pass = 1; else assert_fail = 1;
  assume property (a) assume_pass = 1; else assume_fail = 1;
  assert property (@(posedge fail_clk) always_false);
  cover property (a ##2 b) cover_delay = 1;
  cover property (a[*2] ##1 b) cover_repeat = 1;
  cover property (@(posedge alternate_clk) c) begin
    cover_explicit_1 = 1;
    report_order = report_order * 10 + 1;
  end
  cover property (@(posedge alternate_clk) c) begin
    cover_explicit_2 = 1;
    report_order = report_order * 10 + 2;
  end
  cover property (@(posedge alternate_clk) c) begin
    cover_explicit_3 = 1;
    report_order = report_order * 10 + 3;
  end
  cover property (@(posedge alternate_clk) c) begin
    cover_explicit_4 = 1;
    report_order = report_order * 10 + 4;
  end

  // A false implication antecedent is a vacuous assertion success, but must
  // not count as a cover hit.
  cover property (never |-> b) cover_vacuous = 1;

  initial begin
    #1 begin a = 1; b = 0; end
    // The NBA commits before Observed, but assertion predicates read the
    // Preponed value. These four covers must not hit on this first edge.
    #1 begin c <= 1; alternate_clk = 1; fail_clk = 1; end
    #1 begin alternate_clk = 0; fail_clk = 0; end
    #1 clk = 1;
    #1 clk = 0;
    #1 clk = 1;
    #1 clk = 0;
    #1 begin a = 0; b = 1; end
    #1 clk = 1;
    #1 clk = 0;
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;
    #1 $display("assert=%0d%0d assume=%0d%0d cover=%0d%0d%0d%0d%0d%0d vacuous=%0d order=%0d",
                assert_pass, assert_fail, assume_pass, assume_fail,
                cover_delay, cover_repeat, cover_explicit_1,
                cover_explicit_2, cover_explicit_3, cover_explicit_4,
                cover_vacuous, report_order);
    $finish;
  end
endmodule

// CHECK: ERROR: {{.*}}native-concurrent-sva-observable.sv:{{[0-9]+}}: concurrent assertion failed.
// CHECK: assert=11 assume=11 cover=111111 vacuous=0 order=1234
// SIM-DAG: obelisk_sim.assert.sampled_read
// SIM-DAG: home_region = 8 : i32
// SIM-DAG: home_region = 10 : i32
// SIM-DAG: obelisk_sim.concurrent_report
// SIM-DAG: obelisk_sim.detached_controls
// AOT: call i32 @obelisk_rt_v1_scheduler_add_aot
// AOT: call i32 @obelisk_rt_v1_scheduler_run(
