// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=O0
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null

// `a[*0:1] |=> b` evaluates the empty match's consequent on the current tick
// and retains the nonempty endpoint's consequent for the next tick. The
// weak EOS coordinator closes only the latter pending obligation. Both live
// source ages have one coalesced pass/fail dispatch site.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos_branch_report.
// CHECK: simulation.bytes.constant "imp-pass"
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos_branch.
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK: simulation.branching_antecedent_eos_result = "pass"
// CHECK-SAME: simulation.branching_antecedent_eos_source_age = 1 : i64
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: simulation.mixed_empty_antecedent_current_tick_channels = 1 : i64
// CHECK-SAME: simulation.mixed_empty_antecedent_handoff_channels = 1 : i64
// CHECK-SAME: simulation.mixed_empty_antecedent_nonoverlap
// O0-LABEL: simulation.func private @unit_0(
// O0-COUNT-2: simulation.ref.alloc
// O0: simulation.spawn @unit_0.$concurrent_eos_branch.
// O0-NOT: simulation.ref.alloc
// O0: simulation.suspend.edge posedge
// CHECK: simulation.suspend.edge posedge
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.assert.sampled_read {{%.*}} from %arg2
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.assert.sampled_read {{%.*}} from %arg3
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.branching_antecedent_universal_failure
// CHECK: simulation.branching_antecedent_universal_success
// CHECK: simulation.spawn @unit_0.fork.20.1.1
// CHECK: simulation.spawn @unit_0.fork.20.0.0
// CHECK: simulation.spawn @unit_0.fork.20.1.1
// CHECK: simulation.spawn @unit_0.fork.20.0.0
// CHECK-NOT: simulation.spawn @unit_0.fork.20
// The same two endpoint channels obey followed-by's existential rule. A live
// success resolves immediately; failure waits until all possible antecedent
// matches and consequent obligations are exhausted.
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_eos_branch_report.
// CHECK: simulation.bytes.constant "follow-pass"
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-SAME: simulation.mixed_empty_antecedent_nonoverlap
// CHECK: simulation.assert.sampled_read {{%.*}} from %arg2
// CHECK: simulation.assert.sampled_read {{%.*}} from %arg3
// CHECK: simulation.branching_antecedent_existential_success
// CHECK: simulation.branching_antecedent_existential_failure
// CHECK: simulation.spawn @unit_1.fork.38.1.1
// CHECK: simulation.spawn @unit_1.fork.38.0.0
// CHECK: simulation.spawn @unit_1.fork.38.1.1
// CHECK: simulation.spawn @unit_1.fork.38.0.0
// CHECK-NOT: simulation.spawn @unit_1.fork.38
// A two-age nonempty endpoint and two-age consequent prove that the current
// empty channel and delayed nonempty channel retain distinct relative ages.
// EOS emits ordered completion checks for source ages 3, 2, and 1, and the
// monitor samples c immediately while retaining d as the continuation.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_branch.
// CHECK: simulation.branching_antecedent_eos_source_age = 3 : i64
// CHECK: simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.branching_antecedent_eos_source_age = 1 : i64
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 4 : i64
// CHECK-SAME: simulation.mixed_empty_antecedent_nonoverlap
// O0-LABEL: simulation.func private @unit_2(
// O0-COUNT-4: simulation.ref.alloc
// O0: simulation.spawn @unit_2.$concurrent_eos_branch.
// O0-NOT: simulation.ref.alloc
// O0: simulation.suspend.edge posedge
// CHECK: simulation.assert.sampled_read {{%.*}} from %arg2
// CHECK: simulation.assert.sampled_read {{%.*}} from %arg3
// CHECK: simulation.assert.sampled_read {{%.*}} from %arg5
// CHECK: simulation.assert.sampled_read {{%.*}} from %arg4
// CHECK-COUNT-3: simulation.branching_antecedent_matched_history
// Cover-property has a default-strong consequent. It has no EOS hit actor,
// but either live endpoint can produce the one cover action for its source
// attempt.
// CHECK-NOT: @unit_3.$concurrent_eos
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: simulation.mixed_empty_antecedent_nonoverlap
// CHECK-COUNT-2: simulation.spawn @unit_3.fork.83.0.0
// CHECK-NOT: simulation.spawn @unit_3.fork.83.0.0
// CHECK-NOT: @unit_3.$concurrent_eos
// Disable owns the antecedent, both relative consequent channels, and the
// epoch. It clears all three temporal cells and increments the epoch so queued
// live/EOS reports are suppressed.
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_cancel.
// CHECK-SAME: schedule.concurrent_cancel
// CHECK-COUNT-3: simulation.ref.store {{%.*}} to %arg{{[4-6]}}
// CHECK: simulation.ref.load %arg7
// CHECK: arith.addi
// CHECK: simulation.ref.store {{%.*}} to %arg7
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 3 : i64
// CHECK-SAME: simulation.mixed_empty_antecedent_nonoverlap
// CHECK: simulation.spawn @unit_4.$concurrent_cancel.
// CHECK: simulation.spawn @unit_4.$concurrent_eos_branch.

`timescale 1ns / 1ns
module top(
  input var logic clk,
  input var logic a,
  input var logic b,
  input var logic c,
  input var logic d,
  input var logic en);
  a0: assert property (@(posedge clk) (((a)[*0:1]) |=> b))
    $display("imp-pass");
  else
    $display("imp-fail");
  a1: assert property (@(posedge clk) (((a)[*0:1]) #=# b))
    $display("follow-pass");
  else
    $display("follow-fail");
  a2: assert property (@(posedge clk) ((((a)[*0]) or (a ##1 b)) |=> (c ##1 d)))
    $display("long-pass");
  else
    $display("long-fail");
  a3: cover property (@(posedge clk) (((a)[*0:1]) |=> b))
    $display("cover-hit");
  a4: assert property (@(posedge clk) disable iff (!en) (((a)[*0:1]) |=> (b ##1 c)))
    $display("disable-pass");
  else
    $display("disable-fail");
endmodule
