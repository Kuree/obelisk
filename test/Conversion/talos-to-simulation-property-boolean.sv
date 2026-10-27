// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' -o %t.threaded.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial.mlir
// RUN: diff %t.threaded.mlir %t.serial.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' -o /dev/null
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null

// REQUIRES: z3
// Keep the consensus formula as a cover property. Cover property
// reports one result per attempt, so it is eligible for the same
// compiler-side Z3 minimization as assert and assume directives.
// `not a` reports failure exactly when sampled `a` is true.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: home_region = 8 : i32
// CHECK: [[A0:%.*]] = simulation.assert.sampled_read
// CHECK: [[A:%.*]] = simulation.logic.is_true [[A0]]
// CHECK: cf.cond_br [[A]],
// `a iff b` expands to `(a && b) || (!a && !b)`.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK: arith.xori
// CHECK: arith.xori
// CHECK: arith.ori
// `a implies b` expands to `!a || b`.
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// CHECK: simulation.assert.sampled_read
// CHECK: arith.xori
// CHECK: simulation.assert.sampled_read
// CHECK: arith.ori
// CHECK-NOT: talos.sv.assertion
// Z3 proves `(a && b) || (!a && b)` equivalent to `b` before monitor SSA is
// built, removing one sampled read and the branching state entirely.
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 2 -> 1, literals = 4 -> 1>]
// CHECK-COUNT-1: simulation.assert.sampled_read
// CHECK-NOT: simulation.branching_sequence_monitor
// A non-equivalent else branch keeps both guarded alternatives.
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 2 -> 2, literals = {{[0-9]+}} -> {{[0-9]+}}>]
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// A missing else is a vacuous success when the condition is false. The cover
// pass action executes for either success, while the alternative remains
// marked separately for future vacuity counters.
// CHECK: simulation.func private @[[IF_PASS:unit_5\.fork\.66\.0\.0]](
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: simulation.vacuous_sequence_alternatives = 1 : i64
// CHECK: [[B_BITS:%.*]] = simulation.assert.sampled_read
// CHECK: [[B_TRUTH:%.*]] = simulation.logic.is_true [[B_BITS]]
// CHECK: [[A_BITS:%.*]] = simulation.assert.sampled_read
// CHECK: [[A_TRUTH:%.*]] = simulation.logic.is_true [[A_BITS]]
// CHECK: [[NONVACUOUS_SUCCESS:%.*]] = arith.andi [[B_TRUTH]], [[A_TRUTH]]
// CHECK: [[VACUOUS_SUCCESS:%.*]] = arith.xori [[A_TRUTH]],
// CHECK: [[ANY_SUCCESS:%.*]] = arith.ori [[NONVACUOUS_SUCCESS]], [[VACUOUS_SUCCESS]]
// CHECK: cf.cond_br [[ANY_SUCCESS]],
// CHECK: simulation.spawn @[[IF_PASS]]
// Negating a case with identical matching/default bodies flips both positive
// and negative case guards. The minimizer proves the result is just `!b`,
// eliminating the selector and its four-state equality guard.
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 4 -> 1, literals = 8 -> 1>]
// CHECK-COUNT-1: simulation.assert.sampled_read
// CHECK-NOT: simulation.logic.compare case_eq
// Without a default, no matching label is a vacuous success. The pass action
// executes for that path as well as for a matching label with a true body.
// CHECK: simulation.func private @[[CASE_PASS:unit_7\.fork\.93\.0\.0]](
// CHECK: simulation.ref.load
// CHECK: simulation.ref.store
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: simulation.vacuous_sequence_alternatives = 1 : i64
// CHECK: [[CASE_BODY:%.*]] = simulation.logic.is_true
// CHECK: [[CASE_MATCH:%.*]] = simulation.logic.compare case_eq
// CHECK: [[CASE_SUCCESS:%.*]] = arith.andi [[CASE_BODY]], [[CASE_MATCH]]
// CHECK: [[NO_LABEL:%.*]] = arith.xori [[CASE_MATCH]],
// CHECK: [[ANY_CASE_SUCCESS:%.*]] = arith.ori [[CASE_SUCCESS]], [[NO_LABEL]]
// CHECK: cf.cond_br [[ANY_CASE_SUCCESS]],
// CHECK: simulation.spawn @[[CASE_PASS]]
// `not (a or b)` distributes into the single exact cube `!a && !b`.
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK: simulation.assert.sampled_read
// CHECK: arith.xori
// CHECK: simulation.assert.sampled_read
// CHECK: arith.xori
// CHECK: arith.andi
// CHECK: arith.xori
// CHECK-NOT: simulation.branching_sequence_monitor
// Negating `(!a && !b) || (a && !c) || (!b && !c)` in a cover property
// produces the consensus DNF `(a && c) || (!a && b) || (b && c)`.
// Duplicate/contradiction cleanup cannot remove the consensus cube; Z3 proves
// it redundant before lowering, demonstrating cover-property solver use.
// CHECK-LABEL: simulation.func private @unit_9(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 8 -> 2, literals = 24 -> 4>]
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_sequence_monitor
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read

`timescale 1ns / 1ns
module top;
  logic clk;
  logic a;
  logic b;
  logic c;
  assert property (@(posedge clk) (not a));
  assert property (@(posedge clk) (a iff (b)));
  assert property (@(posedge clk) (a implies b));
  assert property (@(posedge clk) (if (a) b else b));
  assert property (@(posedge clk) (if (a) b else (not b)));
  cover property (@(posedge clk) (if (a) b))
    b = a;
  assert property (@(posedge clk) (not (case (a) 1'b0: b; default: b; endcase)));
  cover property (@(posedge clk) (case (a) 1'b0: b; endcase))
    b = a;
  assert property (@(posedge clk) (not (a or b)));
  cover property (@(posedge clk) (not ((((not a) and (not b)) or (a and (not c))) or ((not b) and (not c)))))
    b = a;
endmodule
