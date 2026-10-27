// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null

// REQUIRES: z3
// cover property ((if (a) b) implies c). The missing else makes !a a
// vacuous true LHS. The result has two vacuous success regions:
// a&&!b (false LHS) and !a&&c (vacuous LHS with true RHS), plus the
// nonvacuous a&&b&&c region. All overlap is coalesced to one pass.
// cover property ((if (a) b) iff (if (c) d)). Both missing-else
// operands are vacuous only together on !a&&!c. Every other successful
// equality is nonvacuous because at least one operand is nonvacuous.
// A missing case default has the same vacuity algebra while retaining
// four-state case equality for its selected branch.
// cover property (((a implies b) implies c) iff
//                 ((d implies e) implies f)). With a=d=0 and c=f=0,
// both inner implications are vacuous true, both enclosing
// implications are vacuous false, and the outer iff is vacuous true.
// Retaining only successful intermediate DNFs would misclassify this
// region as nonvacuous.
// cover property
//   (not ((if (a) b) implies c) or (if (d) e)) and (if (f) a).
// At a=d=f=0 and c=0, the implication is vacuous false, its negation
// and both conditional fallthroughs are vacuous true, and the nested
// or/and result is therefore vacuous true. This exercises every
// supported one-cycle vacuity-composition rule in one expression.
// cover property not not (a implies b). The first not has only a
// false-vacuous region when a is false; the second not must recover
// that region as a vacuous success rather than a plain Boolean cube.
// The exact partition initially emits four successful cubes. Z3 removes one
// redundant cube within its vacuity class without crossing classification.
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 4 -> 3, literals = 11 -> 7>]
// CHECK-SAME: simulation.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: simulation.vacuous_sequence_alternatives = 2 : i64
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-COUNT-1: simulation.spawn @unit_0.fork.14.0.0
// CHECK-NOT: simulation.spawn @unit_0.fork.14.0.0
// `iff` keeps only !a&&!c vacuous. The other four successful regions are
// nonvacuous, including equality through two false operand evaluations.
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 5 -> 5, literals = 16 -> 15>]
// CHECK-SAME: simulation.branching_sequence_alternatives = 5 : i64
// CHECK-SAME: simulation.vacuous_sequence_alternatives = 1 : i64
// CHECK-COUNT-4: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-COUNT-1: simulation.spawn @unit_1.fork.28.0.0
// CHECK-NOT: simulation.spawn @unit_1.fork.28.0.0
// The case form retains one selector comparison while producing the same
// 4->3 solver reduction and two vacuous success regions as the conditional.
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 4 -> 3, literals = 11 -> 7>]
// CHECK-SAME: simulation.branching_sequence_alternatives = 3 : i64
// CHECK-SAME: simulation.vacuous_sequence_alternatives = 2 : i64
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK: simulation.logic.compare case_eq
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-COUNT-1: simulation.spawn @unit_2.fork.44.0.0
// CHECK-NOT: simulation.spawn @unit_2.fork.44.0.0
// Nested false-vacuous results remain distinct until the outer iff is
// classified. The generated monitor still samples each semantic atom once and
// dispatches one cover-property action for the single source evaluation.
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 34 -> 12, literals = 204 -> 53>]
// CHECK-SAME: simulation.branching_sequence_alternatives = 12 : i64
// CHECK-SAME: simulation.vacuous_sequence_alternatives = 5 : i64
// CHECK-COUNT-6: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-COUNT-1: simulation.spawn @unit_3.fork.59.0.0
// CHECK-NOT: simulation.spawn @unit_3.fork.59.0.0
// Nested not/or/and lowering preserves false-vacuous results and minimizes
// only within each successful classification before materializing the monitor.
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 28 -> 8, literals = 160 -> 25>]
// CHECK-SAME: simulation.branching_sequence_alternatives = 8 : i64
// CHECK-SAME: simulation.vacuous_sequence_alternatives = 2 : i64
// CHECK-COUNT-6: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-COUNT-1: simulation.spawn @unit_4.fork.83.0.0
// CHECK-NOT: simulation.spawn @unit_4.fork.83.0.0
// The double-negated implication is truth-equivalent to the original and its
// false-antecedent success remains vacuous across both negations.
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<property, solver = "z3", queries = {{[0-9]+}}, alternatives = 3 -> 2, literals = 6 -> 3>]
// CHECK-SAME: simulation.branching_sequence_alternatives = 2 : i64
// CHECK-SAME: simulation.vacuous_sequence_alternatives = 1 : i64
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-COUNT-1: simulation.spawn @unit_5.fork.108.0.0
// CHECK-NOT: simulation.spawn @unit_5.fork.108.0.0
// CHECK-NOT: talos.sv.assertion

`timescale 1ns / 1ns
module top;
  logic clk;
  logic a;
  logic b;
  logic c;
  logic d;
  logic e;
  logic f;
  logic [1:0] sel;
  cover property (@(posedge clk) ((if (a) b) implies c))
    $display;
  cover property (@(posedge clk) ((if (a) b) iff (if (c) d)))
    $display;
  cover property (@(posedge clk) ((case (sel) 2'b01: b; endcase) implies c))
    $display;
  cover property (@(posedge clk) (((a implies b) implies c) iff ((d implies e) implies f)))
    $display;
  cover property (@(posedge clk) (((not ((if (a) b) implies c)) or (if (d) e)) and (if (f) a)))
    $display;
  cover property (@(posedge clk) (not (not (a implies b))))
    $display;
endmodule
