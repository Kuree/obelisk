// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// (a or b) #-# a. Same-endpoint antecedent matches are distinct
// existential obligations of one followed-by property attempt.
// cover property ((a ##[1:2] b) |-> a). If simulation ends while an
// antecedent endpoint is still possible, that path can no longer
// match; the implication succeeds vacuously once per source attempt.
// assert property ((a ##[1:2] b) #=# a). At EOS, an already matched
// source with its weak nonoverlapped consequent pending succeeds;
// a source with only an unfinished antecedent fails existentially.
// Intrinsically strong s_nexttime overrides assert's default weak
// sequence completion for a pending implication consequent.
// Intrinsically weak nexttime overrides cover property's default
// strong sequence completion for a pending implication consequent.
// [*1:3] expands into the three exact traces a, a##1a, and a##1a##1a.
// The monitor shares one sampled read and keeps distinct endpoint state for
// all three alternatives.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: simulation.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: simulation.branching_sequence_monitor
// CHECK: simulation.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-1: simulation.assert.sampled_read
// CHECK: arith.select
// CHECK: cf.cond_br
// `a within b[*3]` has three exact placements for a while preserving all
// three required b samples.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: simulation.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: simulation.branching_sequence_monitor
// CHECK: simulation.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK: arith.select
// CHECK: cf.cond_br
// Top-level first_match shares the same bounded alternatives but records that
// later live endpoints are suppressed after the first successful endpoint.
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: simulation.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: simulation.branching_sequence_monitor
// CHECK-SAME: simulation.first_match_monitor
// CHECK-SAME: simulation.first_match_priority_groups = 1 : i64
// CHECK: simulation.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK: arith.select
// CHECK: cf.cond_br
// A first_match nested before ##1 records the sub-sequence endpoint on every
// trace. The surviving priority gate cancels the later candidate when the
// earlier endpoint succeeds, while tautological gates fold away.
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_sequence_monitor
// CHECK-SAME: simulation.first_match_monitor
// CHECK-SAME: simulation.first_match_priority_groups = 1 : i64
// CHECK: simulation.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK: arith.andi {{.*}}simulation.first_match_priority
// CHECK: cf.cond_br
// A deterministic two-cycle antecedent uses the same source-age coalescer as
// a branching antecedent. At EOS, an unfinished antecedent is a vacuous
// implication success and a pending weak consequent is also a success. The
// two source ages therefore dispatch exactly two pass actions, oldest first.
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_eos_branch_report.55.pass(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-SAME: simulation.concurrent_eos_report
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_eos_branch.55(
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-3: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_4.$concurrent_eos_branch_report.55.pass{{.*}}simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.spawn @unit_4.$concurrent_eos_branch_report.55.pass{{.*}}simulation.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: simulation.spawn @unit_4.$concurrent_eos_branch_report.55.pass
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: simulation.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 3 : i64
// CHECK-COUNT-3: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_4.$concurrent_eos_branch.55
// CHECK: simulation.branching_antecedent_vacuity
// CHECK: simulation.branching_antecedent_matched_history
// CHECK: simulation.branching_antecedent_universal_failure
// CHECK: simulation.branching_antecedent_universal_success
// CHECK: simulation.branching_antecedent_result_cancel
// CHECK: cf.br {{.*}}simulation.branching_antecedent_backedge
// The nonoverlapped followed-by form keeps a distinct consequent bit-0
// handoff. At EOS that weak pending consequence passes, while an unfinished
// antecedent has produced no match and fails. The age-two decision retains a
// mutually exclusive failure path for matched history without a pending weak
// consequence.
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_eos_branch_report.73.pass(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_eos_branch_report.73.fail(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_eos_branch.73(
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK: [[ONE:%.*]] = arith.constant 1 : i64
// CHECK-COUNT-3: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: [[HANDOFF:%.*]] = arith.andi {{%.*}}, [[ONE]] : i64
// CHECK: simulation.spawn @unit_5.$concurrent_eos_branch_report.73.pass{{.*}}simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.spawn @unit_5.$concurrent_eos_branch_report.73.fail{{.*}}simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.spawn @unit_5.$concurrent_eos_branch_report.73.fail{{.*}}simulation.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: simulation.spawn @unit_5.$concurrent_eos_branch_report.73.
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: home_region = 8 : i32
// CHECK-SAME: simulation.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 3 : i64
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-COUNT-3: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_5.$concurrent_eos_branch.73
// CHECK: arith.extui
// CHECK: simulation.branching_antecedent_existential_failure
// CHECK: simulation.branching_antecedent_matched_history
// CHECK: simulation.branching_antecedent_existential_success
// CHECK: simulation.branching_antecedent_result_cancel
// CHECK: cf.br {{.*}}simulation.branching_antecedent_backedge
// Nested first_match scopes retain independent inner and outer priority
// groups across all four exact endpoint combinations.
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: simulation.first_match_monitor
// CHECK-SAME: simulation.first_match_priority_groups = 2 : i64
// CHECK-COUNT-10: simulation.first_match_priority}
// Two first_match terms concatenated at distinct syntax sites retain one
// scope for the first and two independently activated scopes for the second.
// Success in one activation cannot cancel another activation.
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: simulation.first_match_monitor
// CHECK-SAME: simulation.first_match_priority_groups = 3 : i64
// CHECK-COUNT-8: simulation.first_match_priority}
// A ranged prefix starts the same suffix first_match twice. Those activations
// must have separate priority state so an early completion whose continuation
// fails cannot suppress the later activation and its continuation.
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-SAME: simulation.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: simulation.first_match_monitor
// CHECK-SAME: simulation.first_match_priority_groups = 2 : i64
// Each finite eventually offset starts an independent operand activation.
// CHECK-LABEL: simulation.func private @unit_9(
// CHECK-SAME: simulation.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: simulation.first_match_monitor
// CHECK-SAME: simulation.first_match_priority_groups = 2 : i64
// Finite always likewise keeps each required operand activation independent.
// CHECK-LABEL: simulation.func private @unit_10(
// CHECK-SAME: simulation.branching_sequence_alternatives = 4 : i64
// CHECK-SAME: simulation.first_match_monitor
// CHECK-SAME: simulation.first_match_priority_groups = 2 : i64
// A ranged antecedent retains both exact traces and gives each trace its own
// compact consequent channel. The source-age coalescer delays success until
// every later antecedent endpoint and consequent obligation has resolved,
// reports the first universal failure once, and cancels all remaining state
// for that source attempt. With a four-age result horizon this produces seven
// static result sites (three fail plus four pass), rather than one site per
// channel at each consequence endpoint.
// CHECK: simulation.func private @[[RANGE_PASS:unit_11\.fork\.170\.0\.0]](
// CHECK: simulation.func private @[[RANGE_FAIL:unit_11\.fork\.170\.1\.2]](
// CHECK-LABEL: simulation.func private @unit_11(
// CHECK-SAME: simulation.bounded_antecedent_horizon = 3 : i64
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_monitor
// CHECK-SAME: simulation.branching_antecedent_result_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 4 : i64
// CHECK: arith.andi {{.*}}simulation.branching_antecedent_channel = 0 : i64{{.*}}simulation.branching_antecedent_consequent_trigger
// CHECK: arith.andi {{.*}}simulation.branching_antecedent_channel = 1 : i64{{.*}}simulation.branching_antecedent_consequent_trigger
// CHECK: arith.select {{.*}}simulation.branching_antecedent_matched_history
// CHECK: arith.andi {{.*}}simulation.branching_antecedent_vacuity
// CHECK: arith.andi {{.*}}simulation.branching_antecedent_universal_failure
// CHECK: arith.ori {{.*}}simulation.branching_antecedent_universal_success
// CHECK-DAG: simulation.spawn @[[RANGE_FAIL]]
// CHECK-DAG: simulation.spawn @[[RANGE_FAIL]]
// CHECK-DAG: simulation.spawn @[[RANGE_FAIL]]
// CHECK-DAG: simulation.spawn @[[RANGE_PASS]]
// CHECK-DAG: simulation.spawn @[[RANGE_PASS]]
// CHECK-DAG: simulation.spawn @[[RANGE_PASS]]
// CHECK-DAG: simulation.spawn @[[RANGE_PASS]]
// CHECK-NOT: simulation.spawn @unit_11.fork.170.
// CHECK: simulation.branching_antecedent_result_cancel
// CHECK: cf.br {{.*}}simulation.branching_antecedent_backedge
// Same-endpoint `or` alternatives feed one existential followed-by result.
// Either successful consequent match executes the single pass callback; only
// exhaustion without either success executes the single default failure.
// CHECK: simulation.func private @[[FOLLOW_PASS:unit_12\.fork\.188\.0\.0]](
// CHECK: simulation.display
// CHECK: simulation.func private @[[FOLLOW_FAIL:unit_12\.fork\.188\.1\.2]](
// CHECK: simulation.bytes.constant "ERROR:
// CHECK-LABEL: simulation.func private @unit_12(
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_monitor
// CHECK-SAME: simulation.branching_antecedent_result_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 1 : i64
// CHECK-SAME: simulation.followed_by_monitor
// CHECK: [[A_SAMPLE:%.*]] = simulation.assert.sampled_read %arg0 from %arg2
// CHECK: [[A_MATCH:%.*]] = simulation.logic.is_true [[A_SAMPLE]]
// CHECK: [[B_SAMPLE:%.*]] = simulation.assert.sampled_read %arg0 from %arg3
// CHECK: [[B_MATCH:%.*]] = simulation.logic.is_true [[B_SAMPLE]]
// CHECK: [[ANTECEDENT_ANY:%.*]] = arith.ori [[A_MATCH]], [[B_MATCH]]
// CHECK: arith.andi {{.*}}simulation.branching_antecedent_consequent_trigger
// CHECK: [[CONSEQUENT_ANY:%.*]] = arith.ori
// CHECK: [[FOLLOW_FAILED:%.*]] = arith.ori {{.*}} {simulation.branching_antecedent_existential_failure}
// CHECK: cf.cond_br [[FOLLOW_FAILED]],
// CHECK: simulation.spawn @[[FOLLOW_FAIL]]
// CHECK: cf.cond_br [[CONSEQUENT_ANY]],
// CHECK: simulation.spawn @[[FOLLOW_PASS]]
// CHECK-NOT: simulation.spawn @unit_12.fork.188.
// CHECK: cf.br {{.*}}simulation.branching_antecedent_backedge
// The strong cover consequent has no pending state, but the two ranged
// antecedent traces and their matched history do. At EOS the source-age
// coordinator unions those three words and emits one vacuous cover pass for
// each of the two still-live source ages, oldest first.
// CHECK-LABEL: simulation.func private @unit_13.$concurrent_eos_branch_report.203.pass(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK: simulation.display
// CHECK-LABEL: simulation.func private @unit_13.$concurrent_eos_branch.203(
// CHECK-SAME: %arg1: !simulation.ref<i64>
// CHECK-SAME: %arg2: !simulation.ref<i64>
// CHECK-SAME: %arg3: !simulation.ref<i64>
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-3: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_13.$concurrent_eos_branch_report.203.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.spawn @unit_13.$concurrent_eos_branch_report.203.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: simulation.spawn @unit_13.$concurrent_eos_branch_report.203.pass
// CHECK-NOT: @unit_13.$concurrent_eos_branch_report.203.fail
// CHECK-LABEL: simulation.func private @unit_13(
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "strong"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 3 : i64
// CHECK-COUNT-3: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_13.$concurrent_eos_branch.203
// Nonoverlapped followed-by owns two consequent handoff channels in addition
// to its two ranged-antecedent words and matched history. The weak EOS
// coordinator selects pass only where a consequent is pending; antecedent-only
// ages select the one default failure, with no per-channel duplication.
// CHECK-LABEL: simulation.func private @unit_14.$concurrent_eos_branch_report.218.pass(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK: simulation.display
// CHECK-LABEL: simulation.func private @unit_14.$concurrent_eos_branch_report.218.fail(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK: simulation.bytes.constant "ERROR:
// CHECK-LABEL: simulation.func private @unit_14.$concurrent_eos_branch.218(
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-5: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_14.$concurrent_eos_branch_report.218.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 3 : i64
// CHECK: simulation.spawn @unit_14.$concurrent_eos_branch_report.218.fail{{.*}}simulation.branching_antecedent_eos_result = "fail"{{.*}}simulation.branching_antecedent_eos_source_age = 3 : i64
// CHECK: simulation.spawn @unit_14.$concurrent_eos_branch_report.218.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.spawn @unit_14.$concurrent_eos_branch_report.218.fail{{.*}}simulation.branching_antecedent_eos_result = "fail"{{.*}}simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.spawn @unit_14.$concurrent_eos_branch_report.218.fail{{.*}}simulation.branching_antecedent_eos_result = "fail"{{.*}}simulation.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: simulation.spawn @unit_14.$concurrent_eos_branch_report.218.
// CHECK-LABEL: simulation.func private @unit_14(
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 4 : i64
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-COUNT-5: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_14.$concurrent_eos_branch.218
// An intrinsically strong s_nexttime consequent overrides assert property's
// default weak sequence completion. At EOS, pending consequents fail while
// sources with only an unfinished antecedent still pass vacuously, once per
// source age and oldest first.
// CHECK-LABEL: simulation.func private @unit_15.$concurrent_eos_branch_report.233.pass(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-LABEL: simulation.func private @unit_15.$concurrent_eos_branch_report.233.fail(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-LABEL: simulation.func private @unit_15.$concurrent_eos_branch.233(
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-5: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_15.$concurrent_eos_branch_report.233.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 3 : i64
// CHECK: simulation.spawn @unit_15.$concurrent_eos_branch_report.233.fail{{.*}}simulation.branching_antecedent_eos_result = "fail"{{.*}}simulation.branching_antecedent_eos_source_age = 3 : i64
// CHECK: simulation.spawn @unit_15.$concurrent_eos_branch_report.233.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.spawn @unit_15.$concurrent_eos_branch_report.233.fail{{.*}}simulation.branching_antecedent_eos_result = "fail"{{.*}}simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.spawn @unit_15.$concurrent_eos_branch_report.233.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: simulation.spawn @unit_15.$concurrent_eos_branch_report.233.
// CHECK-LABEL: simulation.func private @unit_15(
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "strong"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 4 : i64
// CHECK-COUNT-5: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_15.$concurrent_eos_branch.233
// Conversely, intrinsically weak nexttime overrides cover property's default
// strong sequence completion. Every active source age succeeds at EOS, and
// no failure report actor or dispatch is emitted.
// CHECK-LABEL: simulation.func private @unit_16.$concurrent_eos_branch_report.249.pass(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-NOT: @unit_16.$concurrent_eos_branch_report.249.fail
// CHECK-LABEL: simulation.func private @unit_16.$concurrent_eos_branch.249(
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-5: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_16.$concurrent_eos_branch_report.249.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 3 : i64
// CHECK: simulation.spawn @unit_16.$concurrent_eos_branch_report.249.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 2 : i64
// CHECK: simulation.spawn @unit_16.$concurrent_eos_branch_report.249.pass{{.*}}simulation.branching_antecedent_eos_result = "pass"{{.*}}simulation.branching_antecedent_eos_source_age = 1 : i64
// CHECK-NOT: simulation.spawn @unit_16.$concurrent_eos_branch_report.249.
// CHECK-LABEL: simulation.func private @unit_16(
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 4 : i64
// CHECK-COUNT-5: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_16.$concurrent_eos_branch.249
// CHECK-NOT: obelisk.sv.assertion

`timescale 1ns / 1ns
module top;
  logic clk;
  logic a;
  logic b;
  assert property (@(posedge clk) ((a)[*1:3]));
  assert property (@(posedge clk) (a within ((b)[*3])));
  assert property (@(posedge clk) (first_match(a ##[1:3] b)));
  assert property (@(posedge clk) ((first_match(a ##[1:2] b)) ##1 a));
  assert property (@(posedge clk) ((a ##1 b) |-> (b ##1 a)))
    $display;
  assert property (@(posedge clk) ((a ##1 b) #=# a))
    $display;
  assert property (@(posedge clk) (first_match((first_match(a ##[1:2] b)) ##1 a ##[1:2] b)));
  assert property (@(posedge clk) ((first_match(a ##[1:2] b)) ##1 (first_match(a ##[1:2] b))));
  assert property (@(posedge clk) (a ##[1:2] b ##1 (first_match(a ##[1:2] b)) ##1 a));
  assert property (@(posedge clk) (eventually [1:2] (first_match(a ##[1:2] b))));
  assert property (@(posedge clk) (always [1:2] (first_match(a ##[1:2] b))));
  assert property (@(posedge clk) ((a ##[1:2] b) |-> (a ##1 b)))
    $display;
  assert property (@(posedge clk) ((a or b) #-# a))
    $display;
  cover property (@(posedge clk) ((a ##[1:2] b) |-> a))
    $display;
  assert property (@(posedge clk) ((a ##[1:2] b) #=# a))
    $display;
  assert property (@(posedge clk) ((a ##[1:2] b) |-> (s_nexttime a)))
    $display;
  cover property (@(posedge clk) ((a ##[1:2] b) |-> (nexttime a)))
    $display;
endmodule
