// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// An asynchronous accept_on over bounded state.  Both report actions compare
// their captured epoch before executing.
// CHECK-LABEL: simulation.func private @unit_0.fork.18.0.0(
// CHECK: %[[U0_PASS_EPOCH:[0-9]+]] = simulation.ref.load %arg2
// CHECK: %[[U0_PASS_VALID:[0-9]+]] = arith.cmpi eq, %[[U0_PASS_EPOCH]], %arg3
// CHECK: cf.cond_br %[[U0_PASS_VALID]],
// CHECK-LABEL: simulation.func private @unit_0.fork.18.1.1(
// CHECK: %[[U0_FAIL_EPOCH:[0-9]+]] = simulation.ref.load %arg2
// CHECK: %[[U0_FAIL_VALID:[0-9]+]] = arith.cmpi eq, %[[U0_FAIL_EPOCH]], %arg3
// CHECK: cf.cond_br %[[U0_FAIL_VALID]],
// The asynchronous disable observer clears the complete bounded state and
// advances the epoch before returning to its priority wait.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_cancel.18(
// CHECK: simulation.ref.store {{.*}} to %arg4
// CHECK: %[[U0_OLD_EPOCH:[0-9]+]] = simulation.ref.load %arg5
// CHECK: %[[U0_NEW_EPOCH:[0-9]+]] = arith.addi %[[U0_OLD_EPOCH]],
// CHECK: simulation.ref.store %[[U0_NEW_EPOCH]] to %arg5
// Each asynchronous abort completion reloads the current disable epoch and
// passes that value to the queued report.  There is one site for each live age.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_abort.18(
// CHECK-SAME: %arg5: !simulation.ref<i64> {simulation.automatic_reference_capture, simulation.capture_kind = 1 : i32}) attributes
// CHECK: %[[U0_ABORT_EPOCH0:[0-9]+]] = simulation.ref.load %arg5
// CHECK-NEXT: {{.*}}simulation.spawn @unit_0.fork.18.0.0(%arg0, %arg4, %arg5, %[[U0_ABORT_EPOCH0]])
// CHECK: %[[U0_ABORT_EPOCH1:[0-9]+]] = simulation.ref.load %arg5
// CHECK-NEXT: {{.*}}simulation.spawn @unit_0.fork.18.0.0(%arg0, %arg4, %arg5, %[[U0_ABORT_EPOCH1]])
// CHECK-NOT: simulation.ref.load %arg5
// On a property clock, the current unsampled disable condition branches before
// the sampled abort condition.  The same state/epoch pair is shared by both
// priority observers and every report.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK: simulation.spawn @unit_0.$concurrent_cancel.18
// CHECK: simulation.context.event
// CHECK: simulation.spawn @unit_0.$concurrent_abort.18
// CHECK: %[[U0_RESET:[0-9]+]] = simulation.ref.load %arg4
// CHECK-NEXT: %[[U0_RESET_TRUE:[0-9]+]] = simulation.logic.is_true %[[U0_RESET]]
// CHECK-NEXT: cf.cond_br %[[U0_RESET_TRUE]], ^bb3
// CHECK: ^bb4:
// CHECK-NEXT: %[[U0_KILL:[0-9]+]] = simulation.assert.sampled_read %arg0 from %arg5
// sync_reject_on needs no asynchronous abort actor, but shares the identical
// cancellation state and epoch.  Its sampled forced result queues the fail
// action with an expected epoch only after the disable branch was false.
// CHECK-NOT: @unit_1.$concurrent_abort
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_cancel.38(
// CHECK: simulation.ref.store {{.*}} to %arg4
// CHECK: %[[U1_OLD_EPOCH:[0-9]+]] = simulation.ref.load %arg5
// CHECK: %[[U1_NEW_EPOCH:[0-9]+]] = arith.addi %[[U1_OLD_EPOCH]],
// CHECK: simulation.ref.store %[[U1_NEW_EPOCH]] to %arg5
// CHECK-NOT: @unit_1.$concurrent_abort
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK: simulation.suspend.edge posedge
// CHECK: %[[U1_RESET:[0-9]+]] = simulation.ref.load %arg4
// CHECK-NEXT: %[[U1_RESET_TRUE:[0-9]+]] = simulation.logic.is_true %[[U1_RESET]]
// CHECK-NEXT: cf.cond_br %[[U1_RESET_TRUE]], ^bb3
// CHECK: ^bb4:
// CHECK-NEXT: %[[U1_KILL:[0-9]+]] = simulation.assert.sampled_read %arg0 from %arg5
// CHECK: ^bb5:
// CHECK: simulation.spawn @unit_1.fork.38.1.1(%arg0, %arg6, {{%[0-9]+}}, {{%[0-9]+}})
// The persistent-delay monitor owns warm-up and eligible cells.  Disable clears
// both and advances one epoch.  The shared counted abort dispatcher also clears
// both, but reloads that epoch once for every dynamically counted fail report.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_cancel.58(
// CHECK: simulation.ref.store {{.*}} to %arg4
// CHECK: simulation.ref.store {{.*}} to %arg5
// CHECK: %[[U2_OLD_EPOCH:[0-9]+]] = simulation.ref.load %arg6
// CHECK: %[[U2_NEW_EPOCH:[0-9]+]] = arith.addi %[[U2_OLD_EPOCH]],
// CHECK: simulation.ref.store %[[U2_NEW_EPOCH]] to %arg6
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_abort_count.58.reject(
// CHECK: %[[U2_WARM:[0-9]+]] = simulation.ref.load %arg1
// CHECK: simulation.ref.store {{.*}} to %arg1
// CHECK: %[[U2_ELIGIBLE:[0-9]+]] = simulation.ref.load %arg2
// CHECK-NEXT: simulation.ref.store {{.*}} to %arg2
// CHECK: %[[U2_ABORT_EPOCH:[0-9]+]] = simulation.ref.load %arg4
// CHECK-NEXT: {{.*}}simulation.spawn @unit_2.fork.58.1.1(%arg0, %arg3, %arg4, %[[U2_ABORT_EPOCH]])
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_abort.58(
// CHECK: simulation.call @unit_2.$concurrent_abort_count.58.reject(%arg0, %arg3, %arg4, %arg5, %arg6,
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.persistent_delay_aggregate_tokens
// CHECK-SAME: simulation.property_abort_action = "reject"
// CHECK: simulation.suspend.edge posedge
// CHECK: %[[U2_RESET:[0-9]+]] = simulation.ref.load %arg4
// CHECK-NEXT: %[[U2_RESET_TRUE:[0-9]+]] = simulation.logic.is_true %[[U2_RESET]]
// CHECK-NEXT: cf.cond_br %[[U2_RESET_TRUE]], ^bb3
// CHECK: ^bb4:
// CHECK-NEXT: %[[U2_KILL:[0-9]+]] = simulation.assert.sampled_read %arg0 from %arg5
// CHECK: ^bb5:
// CHECK: simulation.call @unit_2.$concurrent_abort_count.58.reject({{.*}}%c1_i64
// The fourth unit covers sync_accept_on and the prior chunk's outer temporal
// negation.  There is still no asynchronous abort actor; accept is forced true
// inside the abort and then inverted, so its clocked abort route selects fail.
// CHECK-NOT: @unit_3.$concurrent_abort
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_cancel.78(
// CHECK: simulation.ref.store {{.*}} to %arg4
// CHECK: %[[U3_OLD_EPOCH:[0-9]+]] = simulation.ref.load %arg5
// CHECK: %[[U3_NEW_EPOCH:[0-9]+]] = arith.addi %[[U3_OLD_EPOCH]],
// CHECK: simulation.ref.store %[[U3_NEW_EPOCH]] to %arg5
// CHECK-NOT: @unit_3.$concurrent_abort
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.property_abort_action = "accept"
// CHECK-SAME: simulation.synchronous_property_abort
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-SAME: simulation.temporal_property_negation_outside_abort
// CHECK: simulation.suspend.edge posedge
// CHECK: %[[U3_RESET:[0-9]+]] = simulation.ref.load %arg4
// CHECK-NEXT: %[[U3_RESET_TRUE:[0-9]+]] = simulation.logic.is_true %[[U3_RESET]]
// CHECK-NEXT: cf.cond_br %[[U3_RESET_TRUE]], ^bb3
// CHECK: ^bb4:
// CHECK-NEXT: %[[U3_KILL:[0-9]+]] = simulation.assert.sampled_read %arg0 from %arg5
// CHECK: ^bb7:
// CHECK: simulation.spawn @unit_3.fork.78.1.1(%arg0, %arg6, {{%[0-9]+}}, {{%[0-9]+}})
// CHECK-NOT: @unit_3.$concurrent_abort
// A forced asynchronous accept with no pass action still clears the monitor;
// it needs no expected-epoch operand because it queues no callback.
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_abort.99(
// CHECK: ^bb2:
// CHECK-NEXT: {{.*}}arith.constant {{.*}} 0 : i64
// CHECK-NEXT: simulation.ref.store {{.*}} to %arg3
// CHECK-NEXT: cf.br ^bb1
// CHECK-NOT: simulation.spawn
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"
// A callback-free restrict property is the exact silent boundary: its async
// abort actor captures only context, sampled condition/event, and monitor state.
// The disable epoch exists in the parent but is deliberately absent here.
// CHECK-NOT: @unit_5.fork
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_abort.114(%arg0: !simulation.context {{.*}}, %arg1: !simulation.ref<!simulation.logic<1>> {{.*}}, %arg2: !simulation.event {{.*}}, %arg3: !simulation.ref<i64> {{.*}}) attributes
// CHECK: ^bb2:
// CHECK-NEXT: {{.*}}arith.constant {{.*}} 0 : i64
// CHECK-NEXT: simulation.ref.store {{.*}} to %arg3
// CHECK-NEXT: cf.br ^bb1
// CHECK-NOT: simulation.spawn
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.asynchronous_property_abort
// CHECK-SAME: simulation.property_abort_action = "accept"

`timescale 1ns / 1ns
module top(
  input var logic clk,
  input var logic a,
  input var logic b,
  input var logic reset,
  input var logic kill,
  output var int hit);
  assert property (@(posedge clk) disable iff (reset) (accept_on (kill) (a ##2 b)))
    hit++;
  else
    hit--;
  assert property (@(posedge clk) disable iff (reset) (sync_reject_on (kill) (a ##1 b)))
    hit++;
  else
    hit--;
  assert property (@(posedge clk) disable iff (reset) (reject_on (kill) (a ##[2:$] b)))
    hit++;
  else
    hit--;
  assert property (@(posedge clk) disable iff (reset) (not (sync_accept_on (kill) (a ##1 b))))
    hit++;
  else
    hit--;
  assert property (@(posedge clk) disable iff (reset) (accept_on (kill) (a ##1 b)));
  restrict property (@(posedge clk) disable iff (reset) (accept_on (kill) (a ##1 b)));
endmodule
