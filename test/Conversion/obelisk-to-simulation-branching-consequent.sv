// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// cover property a |-> ((b or c) or (b and c)); the third trace is
// subsumed and a false antecedent still executes the pass action.
// a |=> (b ##[1:2] c)
// disable iff (rst) ((a or b) |=> c), with an observable weak-EOS
// pass whose queued callback must remain epoch-protected.
// disable iff (rst) (b ##[1:2] c)
// disable iff (rst) (b or c): no live state word is required.
// cover property (a or b) |=> (b or c). The matched-history word
// delays vacuity until both antecedent alternatives are exhausted.
// (a or b) |-> (b or c)
// cover property a |-> ((b ##1 c) or (b ##2 c)). Pending temporal
// alternatives are strong by default, so EOS creates no cover pass.
// The overlapped cover consequent samples both exact alternatives on the
// antecedent clock. The alternatives share one property result. Its two pass
// sites are the false-antecedent vacuous success and the ORed consequent
// success, not one callback per alternative.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<consequent, solver = "{{(heuristic|z3)}}", queries = {{[0-9]+}}, alternatives = 3 -> 2, literals = {{[0-9]+}} -> {{[0-9]+}}>]
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_consequent_monitor
// CHECK: simulation.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: [[A_SAMPLE:%.*]] = simulation.assert.sampled_read %arg0 from %arg2
// CHECK: [[A_MATCH:%.*]] = simulation.logic.is_true [[A_SAMPLE]]
// CHECK: [[B_SAMPLE:%.*]] = simulation.assert.sampled_read %arg0 from %arg3
// CHECK: [[B_MATCH:%.*]] = simulation.logic.is_true [[B_SAMPLE]]
// CHECK: [[AB:%.*]] = arith.andi [[A_MATCH]], [[B_MATCH]] {{.*}}simulation.branching_consequent_trigger
// CHECK: [[C_SAMPLE:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[C_MATCH:%.*]] = simulation.logic.is_true [[C_SAMPLE]]
// CHECK: [[AC:%.*]] = arith.andi [[A_MATCH]], [[C_MATCH]] {{.*}}simulation.branching_consequent_trigger
// CHECK: arith.ori [[AB]], [[AC]]
// CHECK: arith.xori [[A_MATCH]],
// CHECK-COUNT-2: simulation.spawn @unit_0.fork.11.0.0
// CHECK-NOT: simulation.spawn @unit_0.fork
// Nonoverlap first stores a bit-0 obligation, then independently advances the
// two ranged exact traces. The asynchronous disable actor clears both exact
// trace states, and the clock path checks the unsampled level before sampling.
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_cancel.34(
// CHECK-SAME: schedule.concurrent_cancel
// CHECK-SAME: schedule.detached_controls
// CHECK-SAME: schedule.priority_signal_resume
// CHECK: [[CANCEL_ZERO0:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: simulation.ref.store [[CANCEL_ZERO0]] to %arg4
// CHECK: [[CANCEL_ZERO1:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: simulation.ref.store [[CANCEL_ZERO1]] to %arg5
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_eos_report.34.weak(
// CHECK-SAME: simulation.concurrent_eos_report
// CHECK: simulation.ref.load %arg1
// CHECK: arith.cmpi eq
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_eos.34.weak(
// CHECK-SAME: schedule.concurrent_eos_coordinator
// CHECK: [[EOS_ALT0:%.*]] = simulation.ref.load %arg1
// CHECK: [[EOS_ALT1:%.*]] = simulation.ref.load %arg2
// CHECK: [[EOS_LIVE:%.*]] = arith.ori [[EOS_ALT0]], [[EOS_ALT1]]
// CHECK: [[EOS_EPOCH2:%.*]] = simulation.ref.load %arg3
// CHECK-NEXT: simulation.spawn @unit_1.$concurrent_eos_report.34.weak(%arg0, %arg3, [[EOS_EPOCH2]])
// CHECK: [[EOS_EPOCH1:%.*]] = simulation.ref.load %arg3
// CHECK-NEXT: simulation.spawn @unit_1.$concurrent_eos_report.34.weak(%arg0, %arg3, [[EOS_EPOCH1]])
// CHECK: [[EOS_EPOCH0:%.*]] = simulation.ref.load %arg3
// CHECK-NEXT: simulation.spawn @unit_1.$concurrent_eos_report.34.weak(%arg0, %arg3, [[EOS_EPOCH0]])
// CHECK-NOT: simulation.spawn @unit_1.$concurrent_eos_report.34.weak
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_consequent_eos_coalescer
// CHECK-SAME: simulation.branching_consequent_monitor
// CHECK-SAME: simulation.branching_consequent_nonoverlapped
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.strong_weak_monitor
// CHECK-COUNT-3: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_1.$concurrent_cancel.34
// CHECK: simulation.spawn @unit_1.$concurrent_eos.34.weak
// CHECK: simulation.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: simulation.ref.load %arg5
// CHECK: cf.cond_br
// CHECK: ^bb{{[0-9]+}}([[STATE0_REF:%[0-9]+]]: !simulation.ref<i64>, [[STATE1_REF:%[0-9]+]]: !simulation.ref<i64>):
// CHECK: [[A_SAMPLE_1:%.*]] = simulation.assert.sampled_read %arg0 from %arg2
// CHECK: [[A_MATCH_1:%.*]] = simulation.logic.is_true [[A_SAMPLE_1]]
// CHECK: [[STATE0:%.*]] = simulation.ref.load [[STATE0_REF]]
// CHECK: [[STATE0_AGE0:%.*]] = arith.andi [[STATE0]], {{%.*}} : i64
// CHECK: [[ACTIVE0:%.*]] = arith.cmpi ne, [[STATE0_AGE0]], {{%.*}} : i64
// CHECK: [[B_SAMPLE_1:%.*]] = simulation.assert.sampled_read %arg0 from %arg3
// CHECK: [[B_MATCH_1:%.*]] = simulation.logic.is_true [[B_SAMPLE_1]]
// CHECK: [[ADVANCE0:%.*]] = arith.andi [[ACTIVE0]], [[B_MATCH_1]] : i1
// CHECK: [[STATE1:%.*]] = simulation.ref.load [[STATE1_REF]]
// CHECK: [[STATE1_AGE0:%.*]] = arith.andi [[STATE1]], {{%.*}} : i64
// CHECK: [[ACTIVE1:%.*]] = arith.cmpi ne, [[STATE1_AGE0]], {{%.*}} : i64
// CHECK: arith.ori [[ACTIVE0]], [[ACTIVE1]] : i1
// CHECK: [[ADVANCE1:%.*]] = arith.andi [[ACTIVE1]], [[B_MATCH_1]] : i1
// CHECK: [[LAUNCH:%.*]] = arith.extui [[A_MATCH_1]] : i1 to i64
// CHECK: [[ADVANCE0_STATE:%.*]] = arith.select [[ADVANCE0]], {{%.*}}, {{%.*}} : i64
// CHECK: [[NEXT0:%.*]] = arith.ori [[LAUNCH]], [[ADVANCE0_STATE]] : i64
// CHECK: [[ADVANCE1_STATE:%.*]] = arith.select [[ADVANCE1]], {{%.*}}, {{%.*}} : i64
// CHECK: [[NEXT1_BASE:%.*]] = arith.ori [[LAUNCH]], [[ADVANCE1_STATE]] : i64
// CHECK: [[NEXT1:%.*]] = arith.ori [[NEXT1_BASE]], {{%.*}} : i64
// CHECK: simulation.ref.store [[NEXT0]] to [[NEXT0_REF:%[0-9]+]]
// CHECK: simulation.ref.store [[NEXT1]] to [[NEXT1_REF:%[0-9]+]]
// CHECK: cf.br {{.*}}({{%[0-9]+}}, [[NEXT0_REF]], [[NEXT1_REF]] : {{.*}}) {{.*}}simulation.branching_consequent_backedge
// A same-endpoint branching antecedent owns matched history and two independent
// nonoverlapped consequent channels. Its disable actor clears all three state
// words, advances the epoch, and its weak-EOS pass reloads that epoch so a
// queued callback becomes stale after cancellation.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_cancel.51(
// CHECK-SAME: %arg4: !simulation.ref<i64>
// CHECK-SAME: %arg5: !simulation.ref<i64>
// CHECK-SAME: %arg6: !simulation.ref<i64>
// CHECK-SAME: %arg7: !simulation.ref<i64>
// CHECK-SAME: schedule.concurrent_cancel
// CHECK: [[ANT_ZERO0:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: simulation.ref.store [[ANT_ZERO0]] to %arg4
// CHECK: [[ANT_ZERO1:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: simulation.ref.store [[ANT_ZERO1]] to %arg5
// CHECK: [[ANT_ZERO2:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: simulation.ref.store [[ANT_ZERO2]] to %arg6
// CHECK-NOT: simulation.ref.store {{.*}} to %arg{{[4-6]}}
// CHECK: [[ANT_EPOCH:%.*]] = simulation.ref.load %arg7
// CHECK: [[ANT_NEXT_EPOCH:%.*]] = arith.addi [[ANT_EPOCH]],
// CHECK: simulation.ref.store [[ANT_NEXT_EPOCH]] to %arg7
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_branch_report.51.pass(
// CHECK-SAME: %arg3: !simulation.ref<i64>
// CHECK-SAME: %arg4: i64
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK: [[EOS_CURRENT_EPOCH:%.*]] = simulation.ref.load %arg3
// CHECK: [[EOS_EPOCH_VALID:%.*]] = arith.cmpi eq, [[EOS_CURRENT_EPOCH]], %arg4
// CHECK: cf.cond_br [[EOS_EPOCH_VALID]],
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_branch.51(
// CHECK-SAME: %arg1: !simulation.ref<i64>
// CHECK-SAME: %arg2: !simulation.ref<i64>
// CHECK-SAME: %arg3: !simulation.ref<i64>
// CHECK-SAME: %arg6: !simulation.ref<i64>
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK: simulation.ref.load %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.ref.load %arg3
// CHECK-NOT: simulation.ref.load %arg{{[1-3]}}
// CHECK: cf.cond_br [[EOS_WEAK_PASS:%.*]], {{.*}} {simulation.branching_antecedent_eos_result = "pass"{{.*}}}
// CHECK: [[EOS_EXPECTED_EPOCH:%.*]] = simulation.ref.load %arg6
// CHECK: simulation.spawn @unit_2.$concurrent_eos_branch_report.51.pass({{.*}}%arg6, [[EOS_EXPECTED_EPOCH]])
// CHECK-NOT: simulation.spawn @unit_2.$concurrent_eos_branch_report.51.pass
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_monitor
// CHECK-COUNT-4: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_2.$concurrent_cancel.51
// CHECK: simulation.spawn @unit_2.$concurrent_eos_branch.51
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.ref.load %arg5
// CHECK: cf.cond_br
// CHECK: cf.br {{.*}}simulation.branching_antecedent_backedge
// A ranged plain sequence uses the same cancellation contract and clears both
// alternative state words before the monitor can start a disabled attempt.
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_cancel.70(
// CHECK-SAME: schedule.concurrent_cancel
// CHECK: [[SEQ_ZERO0:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: simulation.ref.store [[SEQ_ZERO0]] to %arg4
// CHECK: [[SEQ_ZERO1:%.*]] = arith.constant {{.*}} 0 : i64
// CHECK-NEXT: simulation.ref.store [[SEQ_ZERO1]] to %arg5
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_sequence_monitor
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.ref.load %arg4
// CHECK: cf.cond_br
// The all-one-age form has no alternative state captures. The cancellation
// actor still advances its epoch, and the monitor level-gates both predicates.
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_cancel.83(
// CHECK-SAME: %arg4: !simulation.ref<i64>
// CHECK-SAME: schedule.concurrent_cancel
// CHECK-NOT: simulation.ref.store {{.*}} to %arg{{[0-3]}}
// CHECK: simulation.ref.store {{.*}} to %arg4
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_sequence_monitor
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.ref.load %arg4
// CHECK: cf.cond_br
// Both sides may branch when the consequent alternatives are one-cycle. Each
// antecedent match owns its own delayed channel, while B-or-C is sampled and
// ORed once for all channels on the following clock. The source-age coalescer
// ORs the channel failures, waits for every implication obligation, and emits
// one property pass rather than one pass per matching antecedent alternative.
// A second static pass site handles the current no-match vacuous success.
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_monitor
// CHECK-SAME: simulation.branching_antecedent_result_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 2 : i64
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.combined_boolean_branching_monitor
// CHECK-SAME: simulation.combined_boolean_branching_pairs = 4 : i64
// CHECK: simulation.suspend.edge posedge
// CHECK: ^bb{{[0-9]+}}([[MATCHED_HISTORY:%.*]]: i64, [[BOTH_CHANNEL0:%.*]]: i64, [[BOTH_CHANNEL1:%.*]]: i64):
// CHECK: [[BOTH_A:%.*]] = simulation.assert.sampled_read %arg0 from %arg2
// CHECK: [[BOTH_A_MATCH:%.*]] = simulation.logic.is_true [[BOTH_A]]
// CHECK: [[BOTH_B:%.*]] = simulation.assert.sampled_read %arg0 from %arg3
// CHECK: [[BOTH_B_MATCH:%.*]] = simulation.logic.is_true [[BOTH_B]]
// CHECK: [[BOTH_CHANNEL0_BIT:%.*]] = arith.andi [[BOTH_CHANNEL0]], {{%.*}} : i64
// CHECK: [[BOTH_CHANNEL0_ACTIVE:%.*]] = arith.cmpi ne, [[BOTH_CHANNEL0_BIT]], {{%.*}} : i64
// CHECK: [[BOTH_C:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[BOTH_C_MATCH:%.*]] = simulation.logic.is_true [[BOTH_C]]
// CHECK: [[CONSEQUENT_OR:%.*]] = arith.ori [[BOTH_B_MATCH]], [[BOTH_C_MATCH]]
// CHECK: [[CHANNEL0_SUCCEEDED:%.*]] = arith.andi [[BOTH_CHANNEL0_ACTIVE]], [[CONSEQUENT_OR]]
// CHECK: [[CHANNEL0_NOT_SUCCEEDED:%.*]] = arith.xori [[CHANNEL0_SUCCEEDED]],
// CHECK: [[CHANNEL0_FAILED:%.*]] = arith.andi [[BOTH_CHANNEL0_ACTIVE]], [[CHANNEL0_NOT_SUCCEEDED]]
// CHECK: [[BOTH_CHANNEL1_BIT:%.*]] = arith.andi [[BOTH_CHANNEL1]], {{%.*}} : i64
// CHECK: [[BOTH_CHANNEL1_ACTIVE:%.*]] = arith.cmpi ne, [[BOTH_CHANNEL1_BIT]], {{%.*}} : i64
// CHECK: [[CHANNEL1_SUCCEEDED:%.*]] = arith.andi [[BOTH_CHANNEL1_ACTIVE]], [[CONSEQUENT_OR]]
// CHECK: [[CHANNEL1_NOT_SUCCEEDED:%.*]] = arith.xori [[CHANNEL1_SUCCEEDED]],
// CHECK: [[CHANNEL1_FAILED:%.*]] = arith.andi [[BOTH_CHANNEL1_ACTIVE]], [[CHANNEL1_NOT_SUCCEEDED]]
// CHECK: [[ANY_CHANNEL_FAILED:%.*]] = arith.ori [[CHANNEL0_FAILED]], [[CHANNEL1_FAILED]]
// CHECK: [[BOTH_NEXT0:%.*]] = arith.extui [[BOTH_A_MATCH]] : i1 to i64
// CHECK: [[ANTECEDENT_OR:%.*]] = arith.ori [[BOTH_A_MATCH]], [[BOTH_B_MATCH]]
// CHECK: [[BOTH_NEXT1:%.*]] = arith.extui [[BOTH_B_MATCH]] : i1 to i64
// CHECK: arith.select [[ANTECEDENT_OR]], {{.*}} {simulation.branching_antecedent_matched_history}
// CHECK: arith.andi {{.*}} {simulation.branching_antecedent_vacuity}
// CHECK: [[COALESCED_SUCCESS:%.*]] = arith.ori {{.*}} {simulation.branching_antecedent_universal_success}
// CHECK: cf.cond_br [[COALESCED_SUCCESS]],
// CHECK: simulation.spawn @unit_5.fork.96.0.0
// CHECK: cf.cond_br {{.*}},
// CHECK: simulation.spawn @unit_5.fork.96.0.0
// CHECK-NOT: simulation.spawn @unit_5.fork.96.0.0
// CHECK-COUNT-2: simulation.branching_antecedent_result_cancel
// CHECK: cf.br {{.*}} : i64, i64, i64) {{.*}}simulation.branching_antecedent_backedge
// CHECK-NOT: simulation.spawn @unit_5.fork.96.0.0
// The overlapped combined form has no consequent state: both exact
// antecedent matches consume the one shared B-or-C truth on this sample.
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_monitor
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.combined_boolean_branching_monitor
// CHECK-SAME: simulation.combined_boolean_branching_pairs = 4 : i64
// CHECK: simulation.suspend.edge posedge
// CHECK: [[OVERLAP_A:%.*]] = simulation.assert.sampled_read %arg0 from %arg2
// CHECK: [[OVERLAP_A_MATCH:%.*]] = simulation.logic.is_true [[OVERLAP_A]]
// CHECK: [[OVERLAP_B:%.*]] = simulation.assert.sampled_read %arg0 from %arg3
// CHECK: [[OVERLAP_B_MATCH:%.*]] = simulation.logic.is_true [[OVERLAP_B]]
// CHECK: [[OVERLAP_C:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[OVERLAP_C_MATCH:%.*]] = simulation.logic.is_true [[OVERLAP_C]]
// CHECK: [[OVERLAP_OR:%.*]] = arith.ori [[OVERLAP_B_MATCH]], [[OVERLAP_C_MATCH]]
// CHECK: [[OVERLAP_TRIGGER0:%.*]] = arith.andi [[OVERLAP_A_MATCH]], [[OVERLAP_OR]]
// CHECK: [[OVERLAP_FAILED0:%.*]] = arith.xori [[OVERLAP_TRIGGER0]], {{%.*}}
// CHECK: arith.andi [[OVERLAP_A_MATCH]], [[OVERLAP_FAILED0]]
// CHECK: [[OVERLAP_TRIGGER1:%.*]] = arith.andi [[OVERLAP_B_MATCH]], [[OVERLAP_OR]]
// CHECK: [[OVERLAP_FAILED1:%.*]] = arith.xori [[OVERLAP_TRIGGER1]], {{%.*}}
// CHECK: arith.andi [[OVERLAP_B_MATCH]], [[OVERLAP_FAILED1]]
// CHECK-NOT: obelisk.sv.assertion
// A cover-property sequence consequent is strong by default.  Its two
// temporal alternative words are EOS-aware, but pending attempts fail and
// therefore do not create a cover pass actor.
// CHECK-NOT: @unit_7.$concurrent_eos
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_consequent_eos_coalescer
// CHECK-SAME: simulation.branching_consequent_monitor
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.strong_weak_monitor
// CHECK-NOT: @unit_7.$concurrent_eos
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.branching_consequent_trigger
// CHECK: cf.br {{.*}}simulation.branching_consequent_backedge
// CHECK-NOT: @unit_7.$concurrent_eos

`timescale 1ns / 1ns
module top;
  logic clk;
  logic a;
  logic b;
  logic c;
  logic rst;
  cover property (@(posedge clk) (a |-> ((b or c) or (b and c))))
    c = b;
  assert property (@(posedge clk) disable iff (rst) (a |=> (b ##[1:2] c)))
    $display;
  assert property (@(posedge clk) disable iff (rst) ((a or b) |=> c))
    c = b;
  assert property (@(posedge clk) disable iff (rst) (b ##[1:2] c));
  assert property (@(posedge clk) disable iff (rst) (b or c));
  cover property (@(posedge clk) ((a or b) |=> (b or c)))
    c = b;
  assert property (@(posedge clk) ((a or b) |-> (b or c)));
  cover property (@(posedge clk) (a |-> ((b ##1 c) or (b ##2 c))))
    $display;
endmodule
