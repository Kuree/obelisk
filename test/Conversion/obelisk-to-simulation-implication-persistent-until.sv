// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' -o /dev/null
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// Generated from five assertions over one clock:
//   a |-> (a until b)
//   a |=> (b s_until c)
//   a #-# (b until_with c)
//   not (a #=# (b s_until_with c))
//   disable iff (d) a |=> (a until_with b)
// Overlapped implication activates weak `until` on the antecedent clock.
// Reusing `a` as the antecedent and left operand requires only two sampled
// reads. False antecedent and right success pass; left failure fails.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos_count.
// CHECK: simulation.spawn @unit_0.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.persistent_until_implication
// CHECK-SAME: simulation.persistent_until_kind = "until"
// CHECK-NOT: simulation.persistent_until_inclusive
// CHECK-NOT: simulation.persistent_until_nonoverlapped
// CHECK: simulation.spawn @unit_0.$concurrent_eos_count.
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_0.fork.{{[0-9]+}}.0.0
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_0.fork.{{[0-9]+}}.0.0
// CHECK: simulation.spawn @unit_0.fork.{{[0-9]+}}.1.1
// CHECK-NOT: simulation.spawn @unit_0.fork
// Nonoverlapped strong until owns one explicit handoff bit. The EOS
// coordinator counts live and handoff attempts and dispatches strong failure.
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_eos_count.
// CHECK-COUNT-2: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_1.fork.{{[0-9]+}}.1.1
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.persistent_until_kind = "s_until"
// CHECK-SAME: simulation.persistent_until_nonoverlapped
// CHECK-SAME: simulation.persistent_until_strong
// CHECK: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK-NOT: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK: [[HANDOFF:%.*]] = simulation.ref.alloc %{{.*}} {simulation.persistent_implication_handoff}
// CHECK: simulation.spawn @unit_1.$concurrent_eos_count.{{.*}}({{.*}}[[HANDOFF]]
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_1.fork.{{[0-9]+}}.0.0
// CHECK: simulation.ref.store
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_1.fork.{{[0-9]+}}.0.0
// CHECK: simulation.spawn @unit_1.fork.{{[0-9]+}}.1.1
// CHECK-NOT: simulation.spawn @unit_1.fork
// Inclusive weak until requires the left term on its right-success clock.
// Followed-by maps a false antecedent and a failed operand to failure.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_count.
// CHECK: simulation.spawn @unit_2.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-SAME: simulation.persistent_until_inclusive
// CHECK-SAME: simulation.persistent_until_kind = "until_with"
// CHECK-NOT: simulation.persistent_until_nonoverlapped
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_2.fork.{{[0-9]+}}.1.1
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_2.fork.{{[0-9]+}}.0.0
// CHECK: simulation.spawn @unit_2.fork.{{[0-9]+}}.1.1
// CHECK-NOT: simulation.spawn @unit_2.fork
// Temporal `not` switches strong s_until_with to weak outer completion and
// inverts the coalesced followed-by result once. False antecedent and live or
// EOS operand failure become cover hits.
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_eos_count.
// CHECK: simulation.spawn @unit_3.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_until_inclusive
// CHECK-SAME: simulation.persistent_until_kind = "s_until_with"
// CHECK-SAME: simulation.persistent_until_nonoverlapped
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_3.fork.{{[0-9]+}}.0.0
// CHECK: simulation.ref.store
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_3.fork.{{[0-9]+}}.0.0
// CHECK-NOT: simulation.spawn @unit_3.fork
// Disable clears the live and handoff cells and advances the callback epoch.
// The repeated direct `a` reference still shares one sampled truth value.
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_cancel.
// CHECK: simulation.ref.store {{.*}} to %arg4
// CHECK: simulation.ref.store {{.*}} to %arg5
// CHECK: [[EPOCH:%.*]] = simulation.ref.load %arg6
// CHECK: [[NEXT_EPOCH:%.*]] = arith.addi [[EPOCH]],
// CHECK: simulation.ref.store [[NEXT_EPOCH]] to %arg6
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_eos_count.
// CHECK-COUNT-3: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.persistent_until_inclusive
// CHECK-SAME: simulation.persistent_until_nonoverlapped
// CHECK-COUNT-2: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK-NOT: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK: [[DISABLE_HANDOFF:%.*]] = simulation.ref.alloc %{{.*}} {simulation.persistent_implication_handoff}
// CHECK: simulation.spawn @unit_4.$concurrent_cancel.{{.*}}({{.*}}[[DISABLE_HANDOFF]]
// CHECK: simulation.spawn @unit_4.$concurrent_eos_count.{{.*}}({{.*}}[[DISABLE_HANDOFF]]
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read

`timescale 1ns / 1ns
module top(
  input var logic clk,
  input var logic a,
  input var logic b,
  input var logic c,
  input var logic d);
  assert property (@(posedge clk) (a |-> (a until b)))
    $info("u0-pass");
  else
    $info("u0-fail");
  assert property (@(posedge clk) (a |=> (b s_until c)))
    $info("u1-pass");
  else
    $info("u1-fail");
  assert property (@(posedge clk) (a #-# (b until_with c)))
    $info("u2-pass");
  else
    $info("u2-fail");
  cover property (@(posedge clk) (not (a #=# (b s_until_with c))))
    $info("u3-hit");
  assert property (@(posedge clk) disable iff (d) (a |=> (a until_with b)))
    $info("u4-pass");
  else
    $info("u4-fail");
endmodule
