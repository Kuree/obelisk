// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' -o /dev/null
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null

// Generated from five assertions over one clock:
//   a |-> a[*2:$]
//   a |=> (b[->2:$] ##1 c)
//   a #-# (b[=1:3] ##1 c)
//   not (a #=# (b[*1:$] ##1 c))
//   disable iff (d) a |=> (b ##1 c[->1:2] ##1 d)
// Overlapped implication activates a consecutive DFA on the antecedent clock.
// The repeated use of `a` shares one sampled truth value. Pending states close
// through the weak assertion EOS result.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos_count.
// CHECK-COUNT-2: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_0.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.persistent_repetition_implication
// CHECK-SAME: simulation.persistent_repetition_kind = "consecutive"
// CHECK-SAME: simulation.persistent_repetition_min = 2
// CHECK-SAME: simulation.persistent_repetition_states = 2
// CHECK-SAME: simulation.persistent_repetition_unbounded
// CHECK-NOT: simulation.persistent_repetition_nonoverlapped
// CHECK-NOT: simulation.persistent_implication_handoff
// CHECK: simulation.spawn @unit_0.$concurrent_eos_count.
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_0.fork.{{[0-9]+}}.0.0
// CHECK: simulation.spawn @unit_0.fork.{{[0-9]+}}.0.0
// CHECK: simulation.spawn @unit_0.fork.{{[0-9]+}}.1.1
// CHECK-NOT: simulation.spawn @unit_0.fork
// Nonoverlapped implication adds one handoff bit to the four-state unbounded
// goto DFA. EOS counts all four DFA cells plus that pending activation.
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_eos_count.
// CHECK-COUNT-5: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_1.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.persistent_repetition_kind = "goto"
// CHECK-SAME: simulation.persistent_repetition_min = 2
// CHECK-SAME: simulation.persistent_repetition_nonoverlapped
// CHECK-SAME: simulation.persistent_repetition_states = 4
// CHECK-SAME: simulation.persistent_repetition_unbounded
// CHECK-COUNT-4: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK-NOT: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK: [[U1_HANDOFF:%.*]] = simulation.ref.alloc %{{.*}} {simulation.persistent_implication_handoff}
// CHECK: simulation.spawn @unit_1.$concurrent_eos_count.{{.*}}[[U1_HANDOFF]]
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_1.fork.{{[0-9]+}}.0.0
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.logic.compare case_eq
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_1.fork.{{[0-9]+}}.0.0
// CHECK: simulation.spawn @unit_1.fork.{{[0-9]+}}.1.1
// CHECK-NOT: simulation.spawn @unit_1.fork
// Followed-by maps a false antecedent to failure while the finite
// nonconsecutive DFA preserves strict-false/X transitions and live outcomes.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_count.
// CHECK-COUNT-4: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_2.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.followed_by_monitor
// CHECK-SAME: simulation.persistent_repetition_kind = "nonconsecutive"
// CHECK-SAME: simulation.persistent_repetition_max = 3
// CHECK-SAME: simulation.persistent_repetition_min = 1
// CHECK-SAME: simulation.persistent_repetition_states = 4
// CHECK-NOT: simulation.persistent_repetition_nonoverlapped
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_2.fork.{{[0-9]+}}.1.1
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.logic.compare case_eq
// CHECK: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_2.fork.{{[0-9]+}}.0.0
// CHECK: simulation.spawn @unit_2.fork.{{[0-9]+}}.1.1
// CHECK-NOT: simulation.spawn @unit_2.fork
// Temporal not turns the cover-default strong operand into a weak outer
// property. False followed-by activation, live failure, and strong EOS failure
// each become one cover hit; operand success remains action-silent.
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_eos_count.
// CHECK-COUNT-3: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: simulation.spawn @unit_3.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_repetition_kind = "consecutive"
// CHECK-SAME: simulation.persistent_repetition_nonoverlapped
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: [[U3_HANDOFF:%.*]] = simulation.ref.alloc %{{.*}} {simulation.persistent_implication_handoff}
// CHECK: simulation.spawn @unit_3.$concurrent_eos_count.{{.*}}[[U3_HANDOFF]]
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_3.fork.{{[0-9]+}}.0.0
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_3.fork.{{[0-9]+}}.0.0
// CHECK-NOT: simulation.spawn @unit_3.fork
// Disable clears four DFA counters, the bounded prefix bitset, and the
// nonoverlap handoff, then advances the report epoch. EOS observes those six
// cells plus the epoch used to invalidate stale callbacks.
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_cancel.
// CHECK-COUNT-6: simulation.ref.store {{.*}} to %arg{{[4-9]}}
// CHECK: [[U4_EPOCH:%.*]] = simulation.ref.load %arg10
// CHECK: [[U4_NEXT_EPOCH:%.*]] = arith.addi [[U4_EPOCH]],
// CHECK: simulation.ref.store [[U4_NEXT_EPOCH]] to %arg10
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_eos_count.
// CHECK-COUNT-7: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.persistent_repetition_kind = "goto"
// CHECK-SAME: simulation.persistent_repetition_max = 2
// CHECK-SAME: simulation.persistent_repetition_nonoverlapped
// CHECK-SAME: simulation.persistent_repetition_states = 4
// CHECK-COUNT-6: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK-NOT: simulation.ref.alloc %{{.*}} : i64 -> !simulation.ref<i64>
// CHECK: [[U4_HANDOFF:%.*]] = simulation.ref.alloc %{{.*}} {simulation.persistent_implication_handoff}
// CHECK: simulation.spawn @unit_4.$concurrent_cancel.{{.*}}[[U4_HANDOFF]]
// CHECK: simulation.spawn @unit_4.$concurrent_eos_count.{{.*}}[[U4_HANDOFF]]
// CHECK-COUNT-4: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read

`timescale 1ns / 1ns
module top(
  input var logic clk,
  input var logic a,
  input var logic b,
  input var logic c,
  input var logic d);
  assert property (@(posedge clk) (a |-> ((a)[*2:$])))
    $info("u0-pass");
  else
    $info("u0-fail");
  assert property (@(posedge clk) (a |=> (((b)[->2:$]) ##1 c)))
    $info("u1-pass");
  else
    $info("u1-fail");
  assert property (@(posedge clk) (a #-# (((b)[=1:3]) ##1 c)))
    $info("u2-pass");
  else
    $info("u2-fail");
  cover property (@(posedge clk) (not (a #=# (((b)[+]) ##1 c))))
    $info("u3-hit");
  assert property (@(posedge clk) disable iff (d) (a |=> (b ##1 ((c)[->1:2]) ##1 d)))
    $info("u4-pass");
  else
    $info("u4-fail");
endmodule
