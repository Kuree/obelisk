// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' -o /dev/null
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' -o %t.threaded
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial

// One-cycle overlapped implication has no live state. A false antecedent is an
// operand-vacuous success, hence an outer failure; consequent failure becomes
// the sole outer pass, and consequent success becomes an outer failure.
// CHECK-NOT: @unit_0.$concurrent_eos
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_0.fork.16.1.1
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_0.fork.16.0.0
// CHECK: simulation.spawn @unit_0.fork.16.1.1
// CHECK-NOT: simulation.spawn @unit_0.fork
// Cover sees only the nonvacuous inverted consequent failure. Neither a false
// implication antecedent nor a successful consequent creates a hit.
// CHECK-NOT: @unit_1.$concurrent_eos
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_1.fork.33.0.0
// CHECK-NOT: simulation.spawn @unit_1.fork
// Nonoverlapped followed-by retains the matched-antecedent handoff in bit zero.
// Both that handoff and the older pending consequent are finalized as outer
// strong failures at EOS. Live prefix/consequent failures invert to passes,
// consequent success to failure, and a false followed-by antecedent to pass.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_report.47.strong(
// CHECK: simulation.bytes.constant "u2-fail"
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos.47.strong(
// CHECK-COUNT-1: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-COUNT-2: simulation.spawn @unit_2.$concurrent_eos_report.47.strong
// CHECK-NOT: simulation.spawn @unit_2.$concurrent_eos_report.47.strong
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-COUNT-1: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_2.$concurrent_eos.47.strong
// CHECK: simulation.spawn @unit_2.fork.47.0.0
// CHECK: simulation.spawn @unit_2.fork.47.0.0
// CHECK: simulation.spawn @unit_2.fork.47.1.1
// CHECK: simulation.spawn @unit_2.fork.47.0.0
// CHECK-NOT: simulation.spawn @unit_2.fork
// The same nonoverlap handoff is a pending strong operand consequence for
// cover. Its EOS failure and both live consequent failures are nonvacuous hits;
// a false implication antecedent inverts to an outer failure and schedules no
// cover-property pass action.
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_eos_report.67.weak(
// CHECK: simulation.bytes.constant "u3-hit"
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_eos.67.weak(
// CHECK-COUNT-1: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-COUNT-2: simulation.spawn @unit_3.$concurrent_eos_report.67.weak
// CHECK-NOT: simulation.spawn @unit_3.$concurrent_eos_report.67.weak
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-COUNT-1: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_3.$concurrent_eos.67.weak
// CHECK: simulation.spawn @unit_3.fork.67.0.0
// CHECK: simulation.spawn @unit_3.fork.67.0.0
// CHECK-NOT: simulation.spawn @unit_3.fork
// Branching consequent words are unioned before EOS reporting. This preserves
// one property result for each start age instead of one result per alternative.
// Live failure/success sites still select the inverted pass/fail callbacks.
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_eos_report.84.strong(
// CHECK: simulation.bytes.constant "u4-fail"
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_eos.84.strong(
// CHECK: simulation.ref.load
// CHECK: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: arith.ori
// CHECK-COUNT-2: simulation.spawn @unit_4.$concurrent_eos_report.84.strong
// CHECK-NOT: simulation.spawn @unit_4.$concurrent_eos_report.84.strong
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_consequent_monitor
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_4.$concurrent_eos.84.strong
// CHECK: simulation.spawn @unit_4.fork.84.1.1
// CHECK: simulation.spawn @unit_4.fork.84.1.1
// CHECK: simulation.spawn @unit_4.fork.84.0.0
// CHECK: simulation.spawn @unit_4.fork.84.1.1
// CHECK: simulation.spawn @unit_4.fork.84.0.0
// CHECK: simulation.spawn @unit_4.fork.84.0.0
// CHECK-NOT: simulation.spawn @unit_4.fork
// Final unbounded-delay implication keeps prefix, warm-up, and eligible state.
// Counted strong operand failures invert into cover hits at EOS and prefix
// failure clocks, while terminal success and false antecedent stay silent.
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_eos_count.110.delay_strong(
// CHECK-COUNT-3: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_5.fork.110.0.0
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_delay_implication
// CHECK-SAME: simulation.persistent_delay_minimum = 1 : i64
// CHECK-SAME: simulation.persistent_delay_prefix_horizon = 2 : i64
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-COUNT-3: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_5.$concurrent_eos_count.110.delay_strong
// CHECK: simulation.spawn @unit_5.fork.110.0.0
// CHECK-NOT: simulation.spawn @unit_5.fork
// Disable clears the nonoverlap handoff and pending-consequent bits and bumps
// the epoch. Both live and EOS callbacks reload that epoch, so already queued
// inverted reports become stale instead of escaping cancellation.
// CHECK-LABEL: simulation.func private @unit_6.$concurrent_cancel.129(
// CHECK: simulation.ref.store {{.*}} to %arg4
// CHECK: simulation.ref.load %arg5
// CHECK: arith.addi
// CHECK: simulation.ref.store {{.*}} to %arg5
// CHECK-LABEL: simulation.func private @unit_6.$concurrent_eos_report.129.strong(
// CHECK: simulation.ref.load %arg1
// CHECK: arith.cmpi eq, {{.*}}, %arg2
// CHECK-LABEL: simulation.func private @unit_6.$concurrent_eos.129.strong(
// CHECK: simulation.ref.load %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.spawn @unit_6.$concurrent_eos_report.129.strong
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.spawn @unit_6.$concurrent_eos_report.129.strong
// CHECK-NOT: simulation.ref.load
// CHECK-NOT: simulation.spawn @unit_6.$concurrent_eos_report.129.strong
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_6.$concurrent_cancel.129
// CHECK: simulation.spawn @unit_6.$concurrent_eos.129.strong
// CHECK: simulation.ref.load
// CHECK: simulation.spawn @unit_6.fork.129.0.0
// CHECK: simulation.ref.load
// CHECK: simulation.spawn @unit_6.fork.129.0.0
// CHECK: simulation.ref.load
// CHECK: simulation.spawn @unit_6.fork.129.1.1
// CHECK: simulation.ref.load
// CHECK: simulation.spawn @unit_6.fork.129.1.1
// A false followed-by antecedent makes not succeed vacuously. The cover pass
// action still executes even though future vacuity counters must classify it
// separately. Consequent failure and pending strong failure at EOS also pass.
// CHECK-LABEL: simulation.func private @unit_7.$concurrent_eos_report.151.weak(
// CHECK: simulation.bytes.constant "u7-hit"
// CHECK-LABEL: simulation.func private @unit_7.$concurrent_eos.151.weak(
// CHECK-COUNT-1: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-COUNT-1: simulation.spawn @unit_7.$concurrent_eos_report.151.weak
// CHECK-NOT: simulation.spawn @unit_7.$concurrent_eos_report.151.weak
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-COUNT-1: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_7.$concurrent_eos.151.weak
// CHECK-COUNT-2: simulation.spawn @unit_7.fork.151.0.0
// CHECK-NOT: simulation.spawn @unit_7.fork
// Direct overlapped #-# composes the same vacuous pass with two branching
// consequent words. Their pending ages are unioned before the two nonvacuous
// strong-failure EOS reports, while four live failure sites share one action.
// CHECK-LABEL: simulation.func private @unit_8.$concurrent_eos_report.165.weak(
// CHECK: simulation.bytes.constant "u8-hit"
// CHECK-LABEL: simulation.func private @unit_8.$concurrent_eos.165.weak(
// CHECK-COUNT-2: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: arith.ori
// CHECK-COUNT-2: simulation.spawn @unit_8.$concurrent_eos_report.165.weak
// CHECK-NOT: simulation.spawn @unit_8.$concurrent_eos_report.165.weak
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_consequent_monitor
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_8.$concurrent_eos.165.weak
// CHECK-COUNT-4: simulation.spawn @unit_8.fork.165.0.0
// CHECK-NOT: simulation.spawn @unit_8.fork
// Nonoverlapped #=# retains its handoff in a fourth aggregate cell. The false
// antecedent and prefix failures enter the counted live pass dispatcher, while
// finalization loads exactly the prefix, handoff/warm-up, and eligible inventory.
// CHECK-NOT: @unit_9.$concurrent_eos_report.188
// CHECK-NOT: @unit_9.$concurrent_eos.188
// CHECK-LABEL: simulation.func private @unit_9.$concurrent_eos_count.188.delay_strong(
// CHECK-COUNT-4: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_9.fork.188.0.0
// CHECK-LABEL: simulation.func private @unit_9(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_delay_implication
// CHECK-SAME: simulation.persistent_delay_minimum = 1 : i64
// CHECK-SAME: simulation.persistent_delay_nonoverlapped
// CHECK-SAME: simulation.persistent_delay_prefix_horizon = 2 : i64
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-COUNT-4: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_9.$concurrent_eos_count.188.delay_strong
// CHECK-COUNT-2: simulation.spawn @unit_9.fork.188.0.0
// CHECK-NOT: simulation.spawn @unit_9.fork

`timescale 1ns / 1ns
module top(
  input var logic clk,
  input var logic a,
  input var logic b,
  input var logic c,
  input var logic d);
  assert property (@(posedge clk) (not (a |-> b)))
    $info("u0-pass");
  else
    $info("u0-fail");
  cover property (@(posedge clk) (not (a |-> b)))
    $info("u1-hit");
  assert property (@(posedge clk) (not (a #=# (b ##1 c))))
    $info("u2-pass");
  else
    $info("u2-fail");
  cover property (@(posedge clk) (not (a |=> (b ##1 c))))
    $info("u3-hit");
  assert property (@(posedge clk) (not (a |-> ((b ##1 c) or (c ##2 d)))))
    $info("u4-pass");
  else
    $info("u4-fail");
  cover property (@(posedge clk) (not (a |-> (b ##1 c ##[+] d))))
    $info("u5-hit");
  assert property (@(posedge clk) disable iff (d) (not (a |=> (b ##1 c))))
    $info("u6-pass");
  else
    $info("u6-fail");
  cover property (@(posedge clk) (not (a #=# b)))
    $info("u7-hit");
  cover property (@(posedge clk) (not (a #-# ((b ##1 c) or (c ##2 d)))))
    $info("u8-hit");
  cover property (@(posedge clk) (not (a #=# (b ##1 c ##[+] d))))
    $info("u9-hit");
endmodule
