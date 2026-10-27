// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=COUNT
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' -o /dev/null
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' --emit-bytecode -o /dev/null

// IEEE 1800-2017 20.12: Off prevents new concurrent assertion attempts, but
// attempts already executing and their actions continue. Kill additionally
// cancels every live attempt and pending report. The fixture selects every
// assertion with Lock, Unlock, Off, and On; $assertkill selects its first ten
// single-clock assertions across deterministic, branching,
// aggregate-persistent, abort, and disable monitor families. The final
// multi-clock assertion remains an On/Off case because Kill is conservatively
// rejected for detached multi-clock attempts.
// All eleven concurrent directives receive stable IDs. Lock/Unlock mutate
// only runtime control state; Off/On additionally cause exactly one enabled
// query in each monitor and no action-state query.
// COUNT-LABEL: simulation.func private @unit_0(
// COUNT-COUNT-11: simulation.assert.control {{.*}} action <lock> assertion
// COUNT-COUNT-11: simulation.assert.control {{.*}} action <unlock> assertion
// COUNT-COUNT-11: simulation.assert.control {{.*}} action <off> assertion
// COUNT-COUNT-11: simulation.assert.control {{.*}} action <on> assertion
// COUNT-COUNT-10: simulation.assert.control {{.*}} action <kill> assertion
// COUNT-NOT: simulation.assert.control
// COUNT-COUNT-11: simulation.assert.enabled
// COUNT-NOT: simulation.assert.enabled
// COUNT-NOT: simulation.assert.action_state
// A deterministic monitor advances and reports older age bits independently
// of the enable value. Only the current age-zero truth and failure are gated.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.assertion_path = "assertion_control_concurrent.det"
// CHECK-SAME: simulation.assertion_target_id = [[DET_ID:[0-9]+]] : i64
// CHECK: simulation.assert.kill_epoch %arg0 assertion [[DET_ID]] {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[DET_ENABLE:%.*]] = simulation.assert.enabled %arg0 assertion [[DET_ID]] {simulation.concurrent_attempt_enable}
// CHECK: arith.cmpi ne
// CHECK: [[OLD_MASK:%.*]] = arith.andi {{.*}} : i64
// CHECK: [[OLD_ACTIVE:%.*]] = arith.cmpi ne, [[OLD_MASK]], {{.*}} : i64
// CHECK: [[OLD_TRUTH:%.*]] = simulation.logic.is_true
// CHECK: [[OLD_FALSE:%.*]] = arith.xori [[OLD_TRUTH]], {{.*}} : i1
// CHECK: arith.andi [[OLD_ACTIVE]], [[OLD_FALSE]] : i1
// CHECK: [[START_TRUTH:%.*]] = simulation.logic.is_true
// CHECK: arith.andi [[START_TRUTH]], [[DET_ENABLE]] {simulation.concurrent_attempt_start} : i1
// Persistent delay and unary monitors use the same prepared identity. For
// ranged eventuality, Off can create holes in the M-cycle warm-up pipeline;
// its second cell is therefore an age bitset shifted every clock, with only
// the enabled start inserted into bit zero.
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.persistent_delay_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[DELAY_ENABLE:%.*]] = simulation.assert.enabled {{.*}} {simulation.concurrent_attempt_enable}
// CHECK: arith.andi {{.*}}, [[DELAY_ENABLE]]
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.persistent_unary_minimum = 2 : i64
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK-COUNT-3: simulation.ref.store
// CHECK: [[UNARY_ENABLE:%.*]] = simulation.assert.enabled {{.*}} {simulation.concurrent_attempt_enable}
// CHECK: [[UNARY_START:%.*]] = arith.extui [[UNARY_ENABLE]] : i1 to i64
// CHECK: [[ELIGIBLE:%.*]] = simulation.ref.load
// CHECK: [[IMMATURE:%.*]] = simulation.ref.load
// CHECK: [[SHIFTED:%.*]] = arith.shli [[IMMATURE]],
// CHECK: [[RETAINED:%.*]] = arith.andi [[SHIFTED]],
// CHECK: arith.ori [[RETAINED]], [[UNARY_START]] : i64
// Age-insensitive aggregate DFAs add at most the enabled current attempt; the
// already-live count/token state still takes its ordinary transition.
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.persistent_until_monitor
// CHECK: simulation.suspend.edge
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: [[UNTIL_BODY:\^bb[0-9]+]]([[UNTIL_LIVE:%[0-9]+]]: i64):
// CHECK: [[UNTIL_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: [[UNTIL_START:%.*]] = arith.extui [[UNTIL_ENABLE]] : i1 to i64
// CHECK: arith.addi [[UNTIL_LIVE]], [[UNTIL_START]] : i64
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.persistent_repetition_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[REPEAT_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: [[REPEAT_START:%.*]] = arith.extui [[REPEAT_ENABLE]] : i1 to i64
// CHECK: [[REPEAT_TRUTH:%.*]] = simulation.logic.is_true
// CHECK: arith.select [[REPEAT_TRUTH]], {{.*}}, [[REPEAT_START]] : i64
// Both bounded branching engines gate their shared source attempt, rather
// than freezing any per-alternative state word.
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.branching_sequence_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[BRANCH_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: arith.andi {{.*}}, [[BRANCH_ENABLE]] {simulation.concurrent_attempt_start} : i1
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.branching_consequent_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[CONSEQUENT_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: arith.andi {{.*}}, [[CONSEQUENT_ENABLE]] {simulation.concurrent_attempt_start} : i1
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-SAME: simulation.branching_antecedent_monitor
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: simulation.ref.store
// CHECK: [[ANTECEDENT_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: [[ONE_AGE_TRUTH:%.*]] = simulation.logic.is_true
// CHECK: [[ONE_AGE_START:%.*]] = arith.andi [[ONE_AGE_TRUTH]], [[ANTECEDENT_ENABLE]] {simulation.concurrent_attempt_start} : i1
// The one-age alternative's terminal match must be the gated start, not its
// raw truth; otherwise it could still launch a consequent while Off.
// CHECK: arith.andi [[ONE_AGE_START]], {{.*}} : i1
// Abort remains higher priority for existing attempts. Its per-age report
// tests do not use the control query, while the additional attempt on the
// abort clock is dispatched only when enabled.
// CHECK-LABEL: simulation.func private @unit_9(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: [[ABORT_BODY:\^bb[0-9]+]]([[ABORT_STORAGE:%[0-9]+]]: !simulation.ref<i64>):
// CHECK: [[ABORT_STATE:%.*]] = simulation.ref.load [[ABORT_STORAGE]]
// CHECK: [[ABORT_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: simulation.assert.sampled_read
// CHECK: cf.cond_br {{.*}}, [[ABORTED:\^bb[0-9]+]],
// CHECK: [[ABORTED]]:
// CHECK: arith.andi [[ABORT_STATE]],
// CHECK: cf.cond_br [[ABORT_ENABLE]],
// A report guarded by both disable iff and Kill receives each reference
// immediately followed by its scheduled epoch value. This ordering is part of
// the outlined callback ABI and must also be preserved by every spawn.
// CHECK-LABEL: simulation.func private @unit_10.fork.{{[0-9.]+}}(
// CHECK-SAME: %arg5: !simulation.ref<i64>
// CHECK-SAME: %arg6: i64
// CHECK-SAME: %arg7: !simulation.ref<i64>
// CHECK-SAME: %arg8: i64
// CHECK: [[REPORT_KILL:%.*]] = simulation.assert.kill_epoch
// CHECK: arith.cmpi eq, [[REPORT_KILL]], %arg8 : i64
// CHECK: [[REPORT_DISABLE:%.*]] = simulation.ref.load %arg5
// CHECK: arith.cmpi eq, [[REPORT_DISABLE]], %arg6 : i64
// Disable is tested first and still clears live state/advances its epoch. The
// assertion-control query occurs only on the non-disabled edge.
// CHECK-LABEL: simulation.func private @unit_10(
// CHECK: simulation.suspend.edge {{.*}} to [[DISABLE_SAMPLE:\^bb[0-9]+]]
// CHECK: [[DISABLE_SAMPLE]]([[KILL_REF:%[0-9]+]]: !simulation.ref<i64>, [[LIVE_REF:%[0-9]+]]: !simulation.ref<i64>, [[DISABLE_REF:%[0-9]+]]: !simulation.ref<i64>):
// CHECK: simulation.logic.is_true
// CHECK: cf.cond_br {{.*}}, [[DISABLED:\^bb[0-9]+]]{{.*}}, [[CONTROLLED:\^bb[0-9]+]]
// CHECK: [[DISABLED]]{{.*}}:
// CHECK: simulation.ref.store
// CHECK: cf.br
// CHECK: [[CONTROLLED]]{{.*}}:
// CHECK: simulation.assert.kill_epoch {{.*}} {simulation.concurrent_kill_epoch_check}
// CHECK: [[DISABLE_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: arith.andi {{.*}}, [[DISABLE_ENABLE]] {simulation.concurrent_attempt_start} : i1
// CHECK: [[DISABLE_EXPECTED:%.*]] = simulation.ref.load [[DISABLE_REF]]
// CHECK: [[KILL_EXPECTED:%.*]] = simulation.ref.load [[KILL_REF]]
// CHECK: simulation.spawn @unit_10.fork.{{[0-9.]+}}({{.*}}, [[DISABLE_REF]], [[DISABLE_EXPECTED]], [[KILL_REF]], [[KILL_EXPECTED]])
// A multi-clock coordinator admits source tokens only while enabled. Once
// admitted, its bounded ##1 count remains live if Off is applied before the
// destination occurrence; only the final report remains detached.
// CHECK-LABEL: simulation.func private @unit_11(
// CHECK-SAME: simulation.multiclock_sequence_coordinator
// CHECK: simulation.suspend.clock_set
// CHECK: simulation.assert.clock_occurrence.consume
// CHECK: [[MULTI_ENABLE:%.*]] = simulation.assert.enabled
// CHECK: arith.select [[MULTI_ENABLE]]
// CHECK: simulation.spawn @unit_11.fork.

`timescale 1ns / 1ns
module assertion_control_concurrent(
  input var logic clk,
  input var logic clk2,
  input var logic a,
  input var logic b,
  input var logic c,
  input var logic d);
  initial begin
    $assertcontrol(1, 1, 7, 0);
    $assertcontrol(2, 1, 7, 0);
    $assertoff(0);
    $asserton(0);
    // Kill selects the first ten single-clock assertions.
    $assertkill(0, det, delay, unary, until_p, repeat_p, branch, impl, bante,
                abort_p, disable_p);
  end
  det: assert property (@(posedge clk) (a ##2 b));
  delay: assert property (@(posedge clk) (a ##[2:$] b));
  unary: assert property (@(posedge clk) (s_eventually [2:$] a));
  until_p: assert property (@(posedge clk) (a until b));
  repeat_p: assert property (@(posedge clk) ((a)[+]));
  branch: assert property (@(posedge clk) ((a ##1 b) or (a ##2 c)));
  impl: assert property (@(posedge clk) (a |=> ((b ##1 c) or (b ##2 d))));
  bante: assert property (@(posedge clk) ((a or (a ##2 c)) |-> d));
  abort_p: assert property (@(posedge clk) (accept_on (c) (a ##2 b)))
    $display("abort pass");
  else
    $display("abort fail");
  disable_p: assert property (@(posedge clk) disable iff (c) (a ##2 b));
  multi: assert property (@(posedge clk) (##1 a ##1 (@(posedge clk2) b)));
endmodule
