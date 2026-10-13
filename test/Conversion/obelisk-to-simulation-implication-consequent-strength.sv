// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// Generated from six explicitly qualified implication consequents:
//   a |-> strong(b ##2 c)
//   a |=> weak(b or (b ##1 c))
//   (a ##[1:2] b) |-> strong(c ##1 d)
//   a |-> strong(b ##[1:$] c)
//   not (a #=# weak(b[->2:$] ##1 c))
//   disable iff (e) a |=> weak(b ##1 c[=1:$] ##1 d)
// A strong bounded consequent fails every still-live age at EOS. The monitor
// retains the ordinary implication pass for a false antecedent and its live
// pass/fail actions; strength changes only pending completion.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos_report.{{[0-9]+}}.strong
// CHECK: simulation.bytes.constant "u0-fail"
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos.{{[0-9]+}}.strong
// CHECK-COUNT-1: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-COUNT-2: simulation.spawn @unit_0.$concurrent_eos_report.{{[0-9]+}}.strong
// CHECK-NOT: simulation.spawn @unit_0.$concurrent_eos_report
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.consequent_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.strong_weak_monitor
// CHECK-COUNT-1: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_0.$concurrent_eos.{{[0-9]+}}.strong
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// A weak nonoverlapped branching consequent coalesces both alternatives before
// reporting one cover hit per source attempt. Either pending alternative makes
// that attempt succeed at EOS, so both age bits dispatch the pass callback.
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_eos_report.{{[0-9]+}}.weak
// CHECK: simulation.bytes.constant "u1-hit"
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_eos.{{[0-9]+}}.weak
// CHECK-COUNT-2: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-COUNT-2: simulation.spawn @unit_1.$concurrent_eos_report.{{[0-9]+}}.weak
// CHECK-NOT: simulation.spawn @unit_1.$concurrent_eos_report
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.branching_consequent_alternatives = 2
// CHECK-SAME: simulation.branching_consequent_eos_coalescer
// CHECK-SAME: simulation.branching_consequent_intrinsic_eos_strength = "weak"
// CHECK-SAME: simulation.branching_consequent_nonoverlapped
// CHECK-SAME: simulation.consequent_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_1.$concurrent_eos.{{[0-9]+}}.weak
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// A ranged antecedent keeps results keyed by source age. At EOS an unfinished
// antecedent is a vacuous pass, while a matched antecedent with a pending
// strong consequent is a failure. The five state words cover both antecedent
// alternatives, matched history, and the two consequent ages.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_branch_report.{{[0-9]+}}.pass
// CHECK: simulation.bytes.constant "u2-pass"
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_branch_report.{{[0-9]+}}.fail
// CHECK: simulation.bytes.constant "u2-fail"
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_branch.{{[0-9]+}}(
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-5: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-COUNT-5: simulation.spawn @unit_2.$concurrent_eos_branch_report.
// CHECK-NOT: simulation.spawn @unit_2.$concurrent_eos_branch_report
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.bounded_antecedent_horizon = 3
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "strong"
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 4
// CHECK-SAME: simulation.consequent_end_of_simulation_strength = "strong"
// CHECK-COUNT-5: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_2.$concurrent_eos_branch.
// CHECK-COUNT-4: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// Explicit strong overrides the assertion-default weak completion for an
// unbounded delay consequent. EOS counts both the prefix and terminal-ready
// cells and dispatches the ordinary failure action once per pending token.
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_eos_count.{{[0-9]+}}.delay_strong
// CHECK-COUNT-2: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_3.fork.{{[0-9]+}}.1.1
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.consequent_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_delay_implication
// CHECK-SAME: simulation.persistent_delay_minimum = 1
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_3.$concurrent_eos_count.{{[0-9]+}}.delay_strong
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// Temporal not preserves the explicit weak operand classification and derives
// a strong outer cover property. Pending weak successes invert to silent EOS
// failures, so this state-only monitor requires no EOS actor. False
// followed-by activation and live operand failures remain observable hits.
// CHECK-NOT: @unit_4.$concurrent_eos
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.consequent_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.persistent_repetition_kind = "goto"
// CHECK-SAME: simulation.persistent_repetition_nonoverlapped
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_4.fork.{{[0-9]+}}.0.0
// CHECK-NOT: simulation.spawn @unit_4.fork
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: simulation.spawn @unit_4.fork
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_4.fork.{{[0-9]+}}.0.0
// CHECK-NOT: simulation.spawn @unit_4.fork
// Disable clears the two repetition cells, bounded prefix, and nonoverlap
// handoff, then advances the report epoch. The weak EOS coordinator observes
// those four cells plus the epoch and emits cover hits for surviving tokens.
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_cancel.
// CHECK-COUNT-4: simulation.ref.store {{.*}} to %arg{{[4-7]}}
// CHECK-NOT: simulation.ref.store {{.*}} to %arg{{[4-7]}}
// CHECK: [[U5_EPOCH:%.*]] = simulation.ref.load %arg8
// CHECK: [[U5_NEXT_EPOCH:%.*]] = arith.addi [[U5_EPOCH]],
// CHECK: simulation.ref.store [[U5_NEXT_EPOCH]] to %arg8
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_eos_count.{{[0-9]+}}.repetition_weak
// CHECK-COUNT-5: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_5.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.consequent_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: simulation.persistent_repetition_nonoverlapped
// CHECK-COUNT-4: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK-NOT: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK: [[U5_HANDOFF:%.*]] = simulation.ref.alloc %{{.*}} {simulation.persistent_implication_handoff}
// CHECK: simulation.spawn @unit_5.$concurrent_cancel.{{.*}}[[U5_HANDOFF]]
// CHECK: simulation.spawn @unit_5.$concurrent_eos_count.{{.*}}[[U5_HANDOFF]]
// CHECK-COUNT-4: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read

`timescale 1ns / 1ns
module top(
  input var logic clk,
  input var logic a,
  input var logic b,
  input var logic c,
  input var logic d,
  input var logic e);
  assert property (@(posedge clk) (a |-> (strong(b ##2 c))))
    $info("u0-pass");
  else
    $info("u0-fail");
  cover property (@(posedge clk) (a |=> (weak(b or (b ##1 c)))))
    $info("u1-hit");
  assert property (@(posedge clk) ((a ##[1:2] b) |-> (strong(c ##1 d))))
    $info("u2-pass");
  else
    $info("u2-fail");
  assert property (@(posedge clk) (a |-> (strong(b ##[+] c))))
    $info("u3-pass");
  else
    $info("u3-fail");
  cover property (@(posedge clk) (not (a #=# (weak(((b)[->2:$]) ##1 c)))))
    $info("u4-hit");
  cover property (@(posedge clk) disable iff (e) (a |=> (weak(b ##1 ((c)[=1:$]) ##1 d))))
    $info("u5-hit");
endmodule
