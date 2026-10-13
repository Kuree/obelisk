// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' -o %t.threaded
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial

// abort(not(P)) retains the abort operator's forced truth: asynchronous
// accept_on dispatches the pass callback both from its detached observer and
// from the clocked priority path. Ordinary P completion and EOS are still
// inverted by the inner temporal not.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos_report.16.strong(
// CHECK: arith.subi
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_abort.16(
// CHECK-SAME: schedule.concurrent_abort
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK: simulation.ref.load
// CHECK: simulation.spawn @unit_0.fork.16.0.0
// CHECK-NOT: simulation.spawn @unit_0.fork.16.1.1
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-NOT: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.suspend.edge
// CHECK: [[U0_RESET:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[U0_ABORT:%.*]] = simulation.logic.is_true [[U0_RESET]]
// CHECK: cf.cond_br [[U0_ABORT]]
// CHECK: simulation.spawn @unit_0.fork.16.0.0
// CHECK: simulation.assert.sampled_read
// not(accept_on(P)) instead flips the vacuous forced accept result. The same
// live bits therefore dispatch only the failure callback, while the abort
// operator keeps its lexical accept identity.
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_abort.35(
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK: simulation.spawn @unit_1.fork.35.1.1
// CHECK-NOT: simulation.spawn @unit_1.fork.35.0.0
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.suspend.edge
// CHECK: [[U1_RESET:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[U1_ABORT:%.*]] = simulation.logic.is_true [[U1_RESET]]
// CHECK: cf.cond_br [[U1_ABORT]]
// CHECK: simulation.spawn @unit_1.fork.35.1.1
// CHECK: simulation.assert.sampled_read
// With cover property, reject_on outside not forces false and has no pass
// callback to dispatch. Moving not outside reject_on turns that same vacuous
// abort into a successful cover evaluation and invokes the pass callback.
// Unit 3 deliberately uses an unqualified one-clock operand: the retained
// abort wrapper, rather than operand horizon or explicit strength, must select
// the temporal route.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_abort.54(
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK-NOT: simulation.spawn
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.suspend.edge
// CHECK: cf.cond_br
// CHECK-NOT: simulation.spawn @unit_2.fork.54.0.0
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: @unit_3.$concurrent_eos
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_abort.70(
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: simulation.suspend.observe
// CHECK-NOT: simulation.spawn @unit_3.fork.70.0.0
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK-NOT: @unit_3.$concurrent_eos
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.suspend.edge
// CHECK: cf.cond_br
// CHECK: simulation.spawn @unit_3.fork.70.0.0
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: @unit_3.$concurrent_eos
// Synchronous accept_on has no detached observer. Its sampled clock-tick
// priority path makes the same lexical distinction: inner not keeps accept as
// pass, outer not changes it to fail.
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.suspend.edge
// CHECK: [[U4_RESET:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[U4_ABORT:%.*]] = simulation.logic.is_true [[U4_RESET]]
// CHECK: cf.cond_br [[U4_ABORT]]
// CHECK: simulation.spawn @unit_4.fork.83.0.0
// CHECK: simulation.assert.sampled_read
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.suspend.edge
// CHECK: [[U5_RESET:%.*]] = simulation.assert.sampled_read %arg0 from %arg4
// CHECK: [[U5_ABORT:%.*]] = simulation.logic.is_true [[U5_RESET]]
// CHECK: cf.cond_br [[U5_ABORT]]
// CHECK: simulation.spawn @unit_5.fork.102.1.1
// CHECK: simulation.assert.sampled_read
// Persistent sync_reject_on uses the counted dispatcher. Inner not leaves the
// forced rejection false, so a cover has no callback; outer not makes every
// cleared token a vacuous cover success and emits one pass per count.
// CHECK-LABEL: simulation.func private @unit_6.$concurrent_abort_count.121.reject(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: [[U6_ZERO:%.*]] = arith.constant 0 : i64
// CHECK: simulation.ref.store [[U6_ZERO]] to %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.ref.store [[U6_ZERO]] to %arg2
// CHECK-NOT: simulation.ref.load
// CHECK-NOT: simulation.ref.store
// CHECK-NOT: simulation.spawn
// CHECK: simulation.return
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.persistent_delay_monitor
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.call @unit_6.$concurrent_abort_count.121.reject
// CHECK-LABEL: simulation.func private @unit_7.$concurrent_abort_count.137.reject(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: [[U7_ZERO:%.*]] = arith.constant 0 : i64
// CHECK: simulation.ref.load %arg1
// CHECK: simulation.ref.store [[U7_ZERO]] to %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.ref.store [[U7_ZERO]] to %arg2
// CHECK-NOT: simulation.ref.load
// CHECK-NOT: simulation.ref.store
// CHECK: arith.cmpi ne
// CHECK: simulation.spawn @unit_7.fork.137.0.0
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.persistent_delay_monitor
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.call @unit_7.$concurrent_abort_count.137.reject
// The asynchronous counted path makes the complementary accept decision in a
// shared dispatcher used by both the observer and already-true clock path.
// abort(not(P)) dispatches pass; not(abort(P)) dispatches fail.
// CHECK-LABEL: simulation.func private @unit_8.$concurrent_abort_count.153.accept(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: simulation.ref.load %arg1
// CHECK: simulation.ref.store {{.*}} to %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.ref.store {{.*}} to %arg2
// CHECK-NOT: simulation.ref.load
// CHECK-NOT: simulation.ref.store
// CHECK: simulation.spawn @unit_8.fork.153.0.0
// CHECK-LABEL: simulation.func private @unit_8.$concurrent_abort.153(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: [[U8_OBSERVER_ZERO:%.*]] = arith.constant {{.*}}0 : i64
// CHECK: simulation.call @unit_8.$concurrent_abort_count.153.accept({{.*}}, [[U8_OBSERVER_ZERO]])
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.spawn @unit_8.$concurrent_abort.153
// CHECK: [[U8_CLOCK_ONE:%.*]] = arith.constant {{.*}}1 : i64
// CHECK: simulation.call @unit_8.$concurrent_abort_count.153.accept({{.*}}, [[U8_CLOCK_ONE]])
// CHECK-LABEL: simulation.func private @unit_9.$concurrent_abort_count.172.accept(
// CHECK-SAME: simulation.concurrent_abort_counted
// CHECK: simulation.ref.load %arg1
// CHECK: simulation.ref.store {{.*}} to %arg1
// CHECK: simulation.ref.load %arg2
// CHECK: simulation.ref.store {{.*}} to %arg2
// CHECK-NOT: simulation.ref.load
// CHECK-NOT: simulation.ref.store
// CHECK: simulation.spawn @unit_9.fork.172.1.1
// CHECK-LABEL: simulation.func private @unit_9.$concurrent_abort.172(
// CHECK: simulation.observer.bind {{.*}} values(%arg1, %arg2 : !simulation.ref<!simulation.logic<1>>, !simulation.event) captures 1
// CHECK: [[U9_OBSERVER_ZERO:%.*]] = arith.constant {{.*}}0 : i64
// CHECK: simulation.call @unit_9.$concurrent_abort_count.172.accept({{.*}}, [[U9_OBSERVER_ZERO]])
// CHECK-LABEL: simulation.func private @unit_9(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.context.event %arg0[2305843009213693952]
// CHECK: simulation.spawn @unit_9.$concurrent_abort.172
// CHECK: [[U9_CLOCK_ONE:%.*]] = arith.constant {{.*}}1 : i64
// CHECK: simulation.call @unit_9.$concurrent_abort_count.172.accept({{.*}}, [[U9_CLOCK_ONE]])
// Exactly the six asynchronous compositions have observer evaluators. Every
// one reads reset from the canonical Preponed plane; no raw condition load is
// used by the detached evaluator. The four synchronous compositions sample
// only in their assertion-clock monitors.
// CHECK-COUNT-6: simulation.assert.sampled_read %arg0 from %arg1
// CHECK-NOT: simulation.assert.sampled_read %arg0 from %arg1

`timescale 1ns / 1ns
module top(
  input var logic clk,
  input var logic a,
  input var logic b,
  input var logic reset,
  output var int hit);
  assert property (@(posedge clk) (accept_on (reset) (not (a ##2 b))))
    hit++;
  else
    hit--;
  assert property (@(posedge clk) (not (accept_on (reset) (a ##2 b))))
    hit++;
  else
    hit--;
  cover property (@(posedge clk) (reject_on (reset) (not (a ##2 b))))
    hit++;
  cover property (@(posedge clk) (not (reject_on (reset) a)))
    hit++;
  assert property (@(posedge clk) (sync_accept_on (reset) (not (a ##2 b))))
    hit++;
  else
    hit--;
  assert property (@(posedge clk) (not (sync_accept_on (reset) (a ##2 b))))
    hit++;
  else
    hit--;
  cover property (@(posedge clk) (sync_reject_on (reset) (not (a ##[+] b))))
    hit++;
  cover property (@(posedge clk) (not (sync_reject_on (reset) (a ##[+] b))))
    hit++;
  assert property (@(posedge clk) (accept_on (reset) (not (a ##[+] b))))
    hit++;
  else
    hit--;
  assert property (@(posedge clk) (not (accept_on (reset) (a ##[+] b))))
    hit++;
  else
    hit--;
endmodule
