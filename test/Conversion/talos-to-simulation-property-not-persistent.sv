// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' -o /dev/null
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=3' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' -o %t.threaded
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial

// Weak always is intrinsically weak. Negation makes the outer property strong:
// live false results hit the pass action, while weak EOS successes become
// failures once per outstanding aggregate token.
// CHECK-LABEL: simulation.func private @unit_0.$concurrent_eos_count.14.always(
// CHECK: simulation.spawn @unit_0.fork.14.1.1
// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.persistent_unary_kind = "always"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.spawn @unit_0.$concurrent_eos_count.14.always
// CHECK: simulation.assert.sampled_read
// CHECK: simulation.spawn @unit_0.fork.14.0.0
// Strong eventuality becomes weak. Its live successes invert to cover failures
// and therefore dispatch nothing, while every strong EOS failure is a
// nonvacuous outer success and creates one cover hit.
// CHECK-LABEL: simulation.func private @unit_1.$concurrent_eos_count.29.s_eventually(
// CHECK: simulation.spawn @unit_1.fork.29.0.0
// CHECK-LABEL: simulation.func private @unit_1(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_unary_kind = "s_eventually"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.spawn @unit_1.$concurrent_eos_count.29.s_eventually
// CHECK-NOT: simulation.spawn @unit_1.fork
// Weak until has both live results: an operand success becomes outer failure,
// and an operand failure becomes outer success. Its vacuous weak EOS success
// also becomes outer failure.
// CHECK-LABEL: simulation.func private @unit_2.$concurrent_eos_count.41.until_weak(
// CHECK: simulation.spawn @unit_2.fork.41.1.1
// CHECK-LABEL: simulation.func private @unit_2(
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.persistent_until_kind = "until"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.spawn @unit_2.$concurrent_eos_count.41.until_weak
// CHECK: simulation.spawn @unit_2.fork.41.1.1
// CHECK: simulation.spawn @unit_2.fork.41.0.0
// Strong until under cover proves the counted-dispatch preselection uses the
// outer result: both live operand failures and strong EOS failures become
// nonvacuous cover hits; operand successes remain silent.
// CHECK-LABEL: simulation.func private @unit_3.$concurrent_eos_count.58.until_strong(
// CHECK: simulation.spawn @unit_3.fork.58.0.0
// CHECK-LABEL: simulation.func private @unit_3(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_until_kind = "s_until"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.spawn @unit_3.$concurrent_eos_count.58.until_strong
// CHECK: simulation.spawn @unit_3.fork.58.0.0
// CHECK-NOT: simulation.spawn @unit_3.fork
// A default assert sequence is weak. The aggregate unbounded-delay state is
// retained; terminal successes and weak EOS successes both invert to failure.
// CHECK-LABEL: simulation.func private @unit_4.$concurrent_eos_count.72.delay_weak(
// CHECK: simulation.spawn @unit_4.fork.72.1.1
// CHECK-LABEL: simulation.func private @unit_4(
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.persistent_delay_monitor
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.spawn @unit_4.$concurrent_eos_count.72.delay_weak
// CHECK: simulation.spawn @unit_4.fork.72.1.1
// A cover-property sequence is strong by default. Prefix failures and strong
// EOS failures both invert into hits, while terminal successes are silent.
// The deterministic two-age prefix remains compact state.
// CHECK-LABEL: simulation.func private @unit_5.$concurrent_eos_count.87.delay_strong(
// CHECK: simulation.spawn @unit_5.fork.87.0.0
// CHECK-LABEL: simulation.func private @unit_5(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_delay_prefix_horizon = 2 : i64
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.spawn @unit_5.$concurrent_eos_count.87.delay_strong
// CHECK: simulation.spawn @unit_5.fork.87.0.0
// CHECK-NOT: simulation.spawn @unit_5.fork
// Persistent repetition reuses its aggregate DFA. The default weak form
// inverts EOS to failure and swaps both live completion callbacks.
// CHECK-LABEL: simulation.func private @unit_6.$concurrent_eos_count.103.repetition_weak(
// CHECK: simulation.spawn @unit_6.fork.103.1.1
// CHECK-LABEL: simulation.func private @unit_6(
// CHECK-SAME: simulation.end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.persistent_repetition_dfa
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.spawn @unit_6.$concurrent_eos_count.103.repetition_weak
// CHECK: simulation.spawn @unit_6.fork.103.1.1
// CHECK: simulation.spawn @unit_6.fork.103.0.0
// Explicit strong qualification remains inside not. Its EOS failure becomes a
// nonvacuous outer success, and live repetition outcomes are still swapped.
// CHECK-LABEL: simulation.func private @unit_7.$concurrent_eos_count.117.repetition_strong(
// CHECK: simulation.spawn @unit_7.fork.117.0.0
// CHECK-LABEL: simulation.func private @unit_7(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_repetition_dfa
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.spawn @unit_7.$concurrent_eos_count.117.repetition_strong
// CHECK: simulation.spawn @unit_7.fork.117.1.1
// CHECK: simulation.spawn @unit_7.fork.117.0.0
// Disable clears the aggregate count and advances the epoch. The negated EOS
// callback reloads that epoch so a stale queued success cannot execute.
// CHECK-LABEL: simulation.func private @unit_8.$concurrent_cancel.
// CHECK: simulation.ref.store {{.*}} to %arg4
// CHECK: simulation.ref.load %arg5
// CHECK: arith.addi
// CHECK: simulation.ref.store {{.*}} to %arg5
// CHECK-LABEL: simulation.func private @unit_8.$concurrent_eos_count.
// CHECK: [[EPOCH:%.*]] = simulation.ref.load %arg2
// CHECK: simulation.spawn @unit_8.fork.{{[0-9]+}}.0.0(%arg0, %arg2, [[EPOCH]])
// CHECK-LABEL: simulation.func private @unit_8(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_unary_kind = "s_eventually"
// CHECK-SAME: simulation.temporal_property_negation
// CHECK: simulation.spawn @unit_8.$concurrent_cancel.
// CHECK: simulation.spawn @unit_8.$concurrent_eos_count.
// An immature ranged-strong-eventuality attempt remains vacuous through not,
// but its successful cover-property evaluation still executes the pass
// action. The eligible count and immature age bitset preserve classification
// for future vacuity counters while one counted callback handles both. The
// bitset representation also permits assertion-control Off gaps without
// changing an attempt's M-clock age.
// CHECK-LABEL: simulation.func private @unit_9.$concurrent_eos_count.
// CHECK: [[ELIGIBLE:%.*]] = simulation.ref.load %arg1
// CHECK: [[IMMATURE:%.*]] = simulation.ref.load %arg2
// CHECK: cf.br [[BIT_LOOP:\^bb[0-9]+]]([[IMMATURE]], [[ELIGIBLE]] : i64, i64)
// CHECK: [[BIT_LOOP]]([[BITS:%.*]]: i64, [[COUNT:%.*]]: i64)
// CHECK: [[LESS_ONE:%.*]] = arith.subi [[BITS]],
// CHECK: [[NEXT_BITS:%.*]] = arith.andi [[BITS]], [[LESS_ONE]] : i64
// CHECK: [[NEXT_COUNT:%.*]] = arith.addi [[COUNT]],
// CHECK: cf.br [[BIT_LOOP]]([[NEXT_BITS]], [[NEXT_COUNT]] : i64, i64)
// CHECK: simulation.spawn @unit_9.fork.{{[0-9]+}}.0.0
// CHECK-LABEL: simulation.func private @unit_9(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_unary_minimum = 2 : i64
// CHECK-SAME: simulation.temporal_property_negation
// Both classifications remain addressable for the EOS coordinator.
// CHECK-COUNT-2: simulation.ref.alloc
// CHECK: simulation.spawn @unit_9.$concurrent_eos_count.
// CHECK: cf.br {{.*}} : !simulation.ref<i64>, !simulation.ref<i64>
// CHECK-NOT: simulation.spawn @unit_9.fork
// Restrict is also a non-assert/assume directive, so a bare sequence operand is
// strong by default and the negated outer property is weak. It remains
// simulation-silent and therefore outlines neither reports nor EOS work.
// CHECK-NOT: @unit_10.$concurrent_eos_count
// CHECK-LABEL: simulation.func private @unit_10(
// CHECK-SAME: simulation.end_of_simulation_strength = "weak"
// CHECK-SAME: simulation.negated_operand_end_of_simulation_strength = "strong"
// CHECK-SAME: simulation.persistent_delay_monitor
// CHECK-SAME: simulation.temporal_property_negation
// CHECK-NOT: simulation.ref.alloc
// CHECK-NOT: simulation.spawn

`timescale 1ns / 1ns
module top(
  input var logic clk,
  input var logic a,
  input var logic b,
  input var logic c);
  assert property (@(posedge clk) (not (always a)))
    $info("u0-pass");
  else
    $info("u0-fail");
  cover property (@(posedge clk) (not (s_eventually a)))
    $info("u1-hit");
  assert property (@(posedge clk) (not (a until b)))
    $info("u2-pass");
  else
    $info("u2-fail");
  cover property (@(posedge clk) (not (a s_until b)))
    $info("u3-hit");
  assert property (@(posedge clk) (not (##[+] a)))
    $info("u4-pass");
  else
    $info("u4-fail");
  cover property (@(posedge clk) (not (a ##1 b ##[+] c)))
    $info("u5-hit");
  assert property (@(posedge clk) (not ((a)[+])))
    $info("u6-pass");
  else
    $info("u6-fail");
  assert property (@(posedge clk) (not (strong((a)[+]))))
    $info("u7-pass");
  else
    $info("u7-fail");
  assert property (@(posedge clk) disable iff (c) (not (s_eventually a)))
    $info("u8-pass");
  else
    $info("u8-fail");
  cover property (@(posedge clk) (not (s_eventually [2:$] a)))
    $info("u9-hit");
  restrict property (@(posedge clk) (not (##[+] a)));
endmodule
