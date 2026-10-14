// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk -emit-slang -DFOUR_STATE %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=FOUR-STATE
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=NO-DIRECT-FIRST-MATCH-PRIORITY
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=3' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// REQUIRES: z3
// The antecedent consensus cube b&&c is redundant in
// (a&&c)||(!a&&b)||(b&&c), but neither duplicate normalization nor
// ordinary cube subsumption can remove it. Z3 proves the two-cube
// equivalent before the source-attempt coalescer is materialized.
// The raw 2x2 product remains subject to its admission cap, but Z3
// may collapse one side before monitor-shape selection. Here
// (a&&b)||(a&&!b) becomes a, leaving an ordinary two-alternative
// consequent rather than an unsupported combined-branching shape.
// Even after Z3 reduces the raw branching antecedent to a, the
// pending s_nexttime consequent needs the source-age EOS coalescer
// so assert observes its intrinsic strong failure.
// The same preserved one-channel coalescer makes a pending weak
// nexttime consequence execute its cover-property pass at EOS.
// Z3 also may collapse a raw branching antecedent to one multi-age
// trace.  The unfinished a ##1 b trace still needs EOS finalization:
// implication completes vacuously and executes the assert pass.
// Z3 reduces the raw two-cube antecedent to a.  The two distinct
// temporal consequent traces then use the ordinary branching-
// consequent monitor, whose EOS union completes each source attempt
// once with the assert directive's default weak sequence semantics.
// Z3 may also reduce raw temporal RHS branching to one trace:
// ((b&&c)##1a)||((b&&!c)##1a) becomes b##1a.  The resulting
// deterministic implication must retain its weak EOS completion.
// A direct outer first_match consequent is strong/weak equivalent to
// its sequence operand. Erasing that one boundary admits the 3x2
// raw product, after which Z3 removes the antecedent consensus cube
// before the source-age coalescer is materialized. The explicit pass
// action also makes weak EOS completion observable.
// The same equivalence is recognized through a named sequence
// invocation. Only its substituted executable body participates;
// the invocation boundary itself is not a nested priority scope.
// With four-state operands no alternative is redundant: an X a makes both a
// and !a false.
// FOUR-STATE-LABEL: simulation.func private @unit_0(
// FOUR-STATE-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "{{[^"]*}}", queries = {{[0-9]+}}, alternatives = 3 -> 3, literals = {{[0-9]+}} -> {{[0-9]+}}>]
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "z3", queries = {{[0-9]+}}, alternatives = 3 -> 2, literals = 6 -> 4>]
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_result_coalescer
// The a&&c antecedent channel makes its consequent `a` tautologically pass.
// With no observable pass action, canonicalization removes that dead channel's
// sampled c read after the Z3-proven antecedent reduction.
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-COUNT-1: simulation.spawn @unit_0.fork.18.0.2
// CHECK-NOT: simulation.spawn @unit_0.fork.18.0.2
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "z3", queries = {{[0-9]+}}, alternatives = 2 -> 1, literals = 4 -> 1>, #simulation.boolean_minimization<consequent, solver = "z3", queries = {{[0-9]+}}, alternatives = 2 -> 2, literals = 2 -> 2>]
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_consequent_monitor
// CHECK-NOT: simulation.branching_antecedent_monitor
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-NOT: obelisk.sv.assertion
// The raw two-alternative antecedent of unit_2 minimizes to one cube, but its
// strong two-age consequent must retain the source-age coalescer.  At EOS only
// the live consequent obligation fails; the no-match implication result was
// already dispatched vacuously on its sampling clock.
// CHECK-NOT: @unit_2.$concurrent_eos_branch_report.69.pass
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_branch_report.69.fail(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-SAME: simulation.concurrent_eos_report
// CHECK-NOT: @unit_2.$concurrent_eos_branch_report.69.pass
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_branch.69(
// CHECK-SAME: schedule.concurrent_eos_coordinator
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-2: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: cf.cond_br {{.*}} {simulation.branching_antecedent_eos_result = "fail", simulation.branching_antecedent_eos_source_age = 1 : i64}
// CHECK: simulation.spawn @unit_2.$concurrent_eos_branch_report.69.fail{{.*}} {simulation.branching_antecedent_eos_result = "fail", simulation.branching_antecedent_eos_source_age = 1 : i64}
// CHECK-NOT: @unit_2.$concurrent_eos_branch_report.69.pass
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "z3", queries = {{[0-9]+}}, alternatives = 2 -> 1, literals = 4 -> 1>]
// CHECK-SAME: simulation.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "strong"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_result_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 2 : i64
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_2.$concurrent_eos_branch.69
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-NOT: @unit_2.$concurrent_eos_branch_report.69.pass
// Unit_3 proves the dual weak completion: the reduced one-channel monitor
// keeps its EOS state and executes exactly the cover pass for a pending
// nexttime obligation, without manufacturing a failure actor.
// CHECK-NOT: @unit_3.$concurrent_eos_branch_report.92.fail
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_eos_branch_report.92.pass(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-SAME: simulation.concurrent_eos_report
// CHECK-NOT: @unit_3.$concurrent_eos_branch_report.92.fail
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_eos_branch.92(
// CHECK-SAME: schedule.concurrent_eos_coordinator
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-2: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: cf.cond_br {{.*}} {simulation.branching_antecedent_eos_result = "pass", simulation.branching_antecedent_eos_source_age = 1 : i64}
// CHECK: simulation.spawn @unit_3.$concurrent_eos_branch_report.92.pass{{.*}} {simulation.branching_antecedent_eos_result = "pass", simulation.branching_antecedent_eos_source_age = 1 : i64}
// CHECK-NOT: @unit_3.$concurrent_eos_branch_report.92.fail
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "z3", queries = {{[0-9]+}}, alternatives = 2 -> 1, literals = 4 -> 1>]
// CHECK-SAME: simulation.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_result_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 2 : i64
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_3.$concurrent_eos_branch.92
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-NOT: @unit_3.$concurrent_eos_branch_report.92.fail
// Unit_4 closes the other solver-dependent hole: a post-minimized single
// antecedent may itself remain multi-age.  EOS must turn its unfinished
// a ##1 b attempt into one vacuous implication pass.
// CHECK-NOT: @unit_4.$concurrent_eos_branch_report.115.fail
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_eos_branch_report.115.pass(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-SAME: simulation.concurrent_eos_report
// CHECK-NOT: @unit_4.$concurrent_eos_branch_report.115.fail
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_eos_branch.115(
// CHECK-SAME: schedule.concurrent_eos_coordinator
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-2: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK: cf.cond_br {{.*}} {simulation.branching_antecedent_eos_result = "pass", simulation.branching_antecedent_eos_source_age = 1 : i64}
// CHECK: simulation.spawn @unit_4.$concurrent_eos_branch_report.115.pass{{.*}} {simulation.branching_antecedent_eos_result = "pass", simulation.branching_antecedent_eos_source_age = 1 : i64}
// CHECK-NOT: @unit_4.$concurrent_eos_branch_report.115.fail
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "z3", queries = {{[0-9]+}}, alternatives = 2 -> 1, literals = 6 -> 2>]
// CHECK-SAME: simulation.bounded_antecedent_horizon = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_alternatives = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_match_channels = 1 : i64
// CHECK-SAME: simulation.branching_antecedent_result_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 2 : i64
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_4.$concurrent_eos_branch.115
// CHECK-COUNT-2: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-NOT: @unit_4.$concurrent_eos_branch_report.115.fail
// After Z3 collapses unit_5's antecedent, the two temporal RHS traces use the
// ordinary branching-consequent monitor.  Its final coordinator unions both
// state words before dispatching one weak success for each relative source
// age, oldest first.
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_eos_report.143.weak(
// CHECK-SAME: schedule.concurrent_report
// CHECK-SAME: simulation.concurrent_eos_report
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_eos.143.weak(
// CHECK-SAME: schedule.concurrent_eos_coordinator
// CHECK-COUNT-2: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-COUNT-2: simulation.spawn @unit_5.$concurrent_eos_report.143.weak
// CHECK-NOT: simulation.spawn @unit_5.$concurrent_eos_report.143.weak
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "z3", queries = {{[0-9]+}}, alternatives = 2 -> 1, literals = 4 -> 1>]
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_consequent_eos_coalescer
// CHECK-SAME: simulation.branching_consequent_monitor
// CHECK-NOT: simulation.branching_antecedent_monitor
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.strong_weak_monitor
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_5.$concurrent_eos.143.weak
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-NOT: obelisk.sv.assertion
// Unit_6 proves the symmetric post-minimization case: genuine Z3 consensus
// removes RHS branching, but the remaining deterministic b ##1 a state still
// owns one weak EOS completion.
// CHECK-LABEL: simulation.func private @unit_6.$concurrent_eos_report.174.weak(
// CHECK-SAME: schedule.concurrent_report
// CHECK-SAME: simulation.concurrent_eos_report
// CHECK-LABEL: simulation.func private @unit_6.$concurrent_eos.174.weak(
// CHECK-SAME: schedule.concurrent_eos_coordinator
// CHECK-COUNT-1: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-COUNT-1: simulation.spawn @unit_6.$concurrent_eos_report.174.weak
// CHECK-NOT: simulation.spawn @unit_6.$concurrent_eos_report.174.weak
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<consequent, solver = "z3", queries = {{[0-9]+}}, alternatives = 2 -> 1, literals = 6 -> 2>]
// CHECK-NOT: simulation.branching_consequent_monitor
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.strong_weak_monitor
// CHECK-COUNT-1: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_6.$concurrent_eos.174.weak
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-NOT: obelisk.sv.assertion
// Unit_7 erases only the direct consequent first_match boundary. The raw 3x2
// pair count is admitted before Z3 removes the antecedent consensus cube; the
// materialized coalescer therefore owns 2x2 channels. Weak EOS completion
// reports the explicit pass once per pending source age, and no runtime
// first_match priority operation remains.
// CHECK-NOT: @unit_7.$concurrent_eos_branch_report.202.fail
// CHECK-LABEL: simulation.func private @unit_7.$concurrent_eos_branch_report.202.pass(
// CHECK-SAME: simulation.branching_antecedent_eos_report
// CHECK-SAME: simulation.concurrent_eos_report
// CHECK-NOT: @unit_7.$concurrent_eos_branch_report.202.fail
// CHECK-LABEL: simulation.func private @unit_7.$concurrent_eos_branch.202(
// CHECK-SAME: schedule.concurrent_eos_coordinator
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-COUNT-5: simulation.ref.load
// CHECK-NOT: simulation.ref.load
// CHECK-COUNT-2: simulation.spawn @unit_7.$concurrent_eos_branch_report.202.pass
// CHECK-NOT: simulation.spawn @unit_7.$concurrent_eos_branch_report.202.pass
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "z3", queries = {{[0-9]+}}, alternatives = 3 -> 2, literals = 6 -> 4>]
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_consequent_eos_strength = "weak"
// CHECK-SAME: simulation.branching_antecedent_eos_coalescer
// CHECK-SAME: simulation.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_result_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 3 : i64
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.combined_bounded_branching_monitor
// CHECK-SAME: simulation.combined_bounded_branching_pairs = 4 : i64
// CHECK-SAME: simulation.combined_bounded_branching_pairs_before_minimization = 6 : i64
// CHECK-SAME: simulation.consequent_first_match_equivalence
// CHECK-SAME: simulation.first_match_monitor
// CHECK-COUNT-5: simulation.ref.alloc
// CHECK-NOT: simulation.ref.alloc
// CHECK: simulation.spawn @unit_7.$concurrent_eos_branch.202
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-NOT: simulation.first_match_priority
// CHECK-NOT: obelisk.sv.assertion
// Unit_8 proves that directness is determined from a named sequence's expanded
// executable body, not the retained assertion-instance inventory. Its outer
// first_match boundary is erased while the two source alternatives and two
// consequent endpoints remain distinct coalescer channels.
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-SAME: simulation.branching_antecedent_alternatives = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_match_channels = 2 : i64
// CHECK-SAME: simulation.branching_antecedent_result_coalescer
// CHECK-SAME: simulation.branching_antecedent_result_horizon = 3 : i64
// CHECK-SAME: simulation.branching_consequent_alternatives = 2 : i64
// CHECK-SAME: simulation.combined_bounded_branching_monitor
// CHECK-SAME: simulation.combined_bounded_branching_pairs = 4 : i64
// CHECK-SAME: simulation.combined_bounded_branching_pairs_before_minimization = 4 : i64
// CHECK-SAME: simulation.consequent_first_match_equivalence
// CHECK-SAME: simulation.first_match_monitor
// CHECK-COUNT-3: simulation.assert.sampled_read
// CHECK-NOT: simulation.assert.sampled_read
// CHECK-NOT: simulation.first_match_priority
// CHECK-NOT: obelisk.sv.assertion
// NO-DIRECT-FIRST-MATCH-PRIORITY-LABEL: simulation.func private @unit_7(
// NO-DIRECT-FIRST-MATCH-PRIORITY-NOT: simulation.first_match_priority

module top;
  logic clk;
  // IEEE 1800-2023 16.6 reads sequence Booleans like if conditions, so !a is
  // the complement of a only when a cannot be X or Z.
`ifdef FOUR_STATE
  logic a, b, c;
`else
  bit a, b, c;
`endif
  sequence fm;
    first_match(a ##[1:2] c);
  endsequence
  assert property (@(posedge clk) ((((a and c) or (!a and b)) or (b and c)) |-> a));
  assert property (@(posedge clk) (((a and b) or (a and !b)) |-> (b or c)));
  assert property (@(posedge clk) (((a and b) or (a and !b)) |-> (s_nexttime c)))
    $display;
  cover property (@(posedge clk) (((a and b) or (a and !b)) |-> (nexttime c)))
    $display;
  assert property (@(posedge clk) (((a ##1 (b and c)) or (a ##1 (b and !c))) |-> a))
    $display;
  assert property (@(posedge clk) (((a and b) or (a and !b)) |-> ((b ##1 c) or (b ##2 c))))
    $display;
  assert property (@(posedge clk) (a |-> (((b and c) ##1 a) or ((b and !c) ##1 a))))
    $display;
  assert property (@(posedge clk) ((((a and c) or (!a and b)) or (b and c)) |-> (first_match(a ##[1:2] b))))
    $display;
  assert property (@(posedge clk) ((a or b) |-> fm));
endmodule
