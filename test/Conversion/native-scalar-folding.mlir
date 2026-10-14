// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(fold-native-scalar-values)))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(fold-native-scalar-values)))' -o %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(simulation.func(fold-native-scalar-values)))' -o %t.serial
// RUN: diff %t.threaded %t.serial
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=PLANE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(fold-native-scalar-values,obelisk-sim-mark-bounded-loops)))' | FileCheck %s --check-prefix=LOOP

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", native.max_state_domain_functions = 0 : i64} {
  simulation.design @folding {
    simulation.scope.decl 0
    // Constant body data can fold without hiding the one-iteration recurrence
    // from scheduling's contextual memory analysis.
    simulation.code_unit.decl 10 in 0 function hierarchy "single_iteration"
    simulation.func @single_iteration(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.br ^header(%zero : i8)
    ^header(%iv: i8):
      %test = arith.cmpi slt, %iv, %one : i8
      cf.cond_br %test, ^body, ^exit
    ^body:
      %data = arith.subi %iv, %iv : i8
      simulation.ref.store %data to %out : i8, !simulation.ref<i8>
      %next = arith.addi %iv, %one : i8
      cf.br ^header(%next : i8)
    ^exit:
      simulation.return
    }
    // LOOP-LABEL: simulation.func @single_iteration
    // LOOP: cf.cond_br {{.*}}schedule.bounded_loop_header
    // LOOP: %[[ZERO:.*]] = arith.constant 0 : i8
    // LOOP: simulation.ref.store %[[ZERO]]
    // LOOP: %[[NEXT:.*]] = arith.addi
    // LOOP-NEXT: cf.br {{.*}}(%[[NEXT]] : i8) {schedule.bounded_loop_latch}
    simulation.storage.decl 0 in 0 : !simulation.unpacked_array<0 : 3 x !simulation.logic<8>> design hierarchy "folding.elements"
    simulation.code_unit.decl 1 in 0 function hierarchy "modular"
    simulation.func @modular(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}) -> i8 attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %four = arith.constant 4 : i8
      %three = arith.constant 3 : i8
      %scaled = arith.muli %seed, %four : i8
      %sum = arith.addi %scaled, %three : i8
      %result = arith.subi %sum, %scaled : i8
      simulation.return %result : i8
    }
    // CHECK-LABEL: simulation.func @modular
    // CHECK: arith.subi
    // CHECK: %[[THREE:.*]] = arith.constant 3 : i8
    // CHECK-NEXT: simulation.return %[[THREE]] : i8

    simulation.code_unit.decl 2 in 0 function hierarchy "known_logic"
    simulation.func @known_logic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %known = simulation.logic.from_bits %seed : i8 -> !simulation.logic<8>
      %result = simulation.logic.binary sub %known, %known : !simulation.logic<8>
      simulation.return %result : !simulation.logic<8>
    }
    // CHECK-LABEL: simulation.func @known_logic
    // CHECK: %[[ZERO:.*]] = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
    // CHECK-NEXT: simulation.return %[[ZERO]] : !simulation.logic<8>

    simulation.code_unit.decl 4 in 0 function hierarchy "known_increment"
    simulation.func @known_increment(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: i8 {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %known = simulation.logic.from_bits %seed : i8 -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %sum = simulation.logic.binary add %known, %one : !simulation.logic<8>
      simulation.return %sum : !simulation.logic<8>
    }
    // CHECK-LABEL: simulation.func @known_increment
    // CHECK: simulation.logic.binary add {{.*}}analysis.known_logic_result
    // PLANE-LABEL: llvm.func @known_increment
    // PLANE-NOT: llvm.select
    // PLANE: llvm.add
    // PLANE-NOT: llvm.select
    // PLANE: llvm.return

    simulation.code_unit.decl 3 in 0 function hierarchy "unknown_logic"
    simulation.func @unknown_logic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %seed: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %result = simulation.logic.binary sub %seed, %seed : !simulation.logic<8>
      simulation.return %result : !simulation.logic<8>
    }
    // CHECK-LABEL: simulation.func @unknown_logic
    // CHECK: %[[UNKNOWN:.*]] = simulation.logic.binary sub
    // CHECK-NEXT: simulation.return %[[UNKNOWN]] : !simulation.logic<8>
    // PLANE-LABEL: llvm.func @unknown_logic
    // PLANE: llvm.select

    // Inputs outside the bounded integer slice still carry X/Z. They cannot
    // become known merely because a comparison or truncation is narrow.
    simulation.code_unit.decl 5 in 0 function hierarchy "wide_unknown_compare"
    simulation.func @wide_unknown_compare(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %lhs: !simulation.logic<256> {simulation.capture_kind = 2 : i32}, %rhs: !simulation.logic<256> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<1> attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %result = simulation.logic.compare eq %lhs, %rhs : (!simulation.logic<256>, !simulation.logic<256>) -> !simulation.logic<1>
      simulation.return %result : !simulation.logic<1>
    }
    // CHECK-LABEL: simulation.func @wide_unknown_compare
    // CHECK-NOT: analysis.known_logic_result
    // CHECK: simulation.logic.compare eq
    // CHECK-NOT: analysis.known_logic_result
    // CHECK: simulation.return
    // PLANE-LABEL: llvm.func @wide_unknown_compare
    // PLANE: llvm.or
    // PLANE: llvm.return

    simulation.code_unit.decl 6 in 0 function hierarchy "wide_unknown_narrow"
    simulation.func @wide_unknown_narrow(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.logic<256> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %result = simulation.logic.resize %input signed = false : !simulation.logic<256> -> !simulation.logic<8>
      simulation.return %result : !simulation.logic<8>
    }
    // CHECK-LABEL: simulation.func @wide_unknown_narrow
    // CHECK-NOT: analysis.known_logic_result
    // CHECK: simulation.logic.resize
    // CHECK-NOT: analysis.known_logic_result
    // CHECK: simulation.return

    simulation.code_unit.decl 7 in 0 function hierarchy "proven_handle"
    simulation.code_unit.decl 9 in 0 function hierarchy "consume_handle"
    simulation.func private @consume_handle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %element: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64}
    simulation.func @proven_handle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i8 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %mask = arith.constant 3 : i8
      %bounded = arith.andi %index, %mask : i8
      %wide = arith.extui %bounded : i8 to i64
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<8>>>
      %element = simulation.ref.array_element %root[%wide] : (!simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<8>>>, i64) -> !simulation.ref<!simulation.logic<8>>
      simulation.call @consume_handle(%ctx, %element) : (!simulation.context, !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.return
    }
    // PLANE-LABEL: llvm.func @proven_handle
    // PLANE-NOT: llvm.icmp
    // PLANE-NOT: llvm.call @obelisk_rt_v1_native_handle_offset
    // PLANE: llvm.add
    // PLANE-NOT: llvm.call @obelisk_rt_v1_native_handle_offset
    // PLANE: llvm.return

    simulation.code_unit.decl 8 in 0 function hierarchy "unproven_handle"
    simulation.func @unproven_handle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i64 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<8>>>
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<8>>>, i64) -> !simulation.ref<!simulation.logic<8>>
      simulation.call @consume_handle(%ctx, %element) : (!simulation.context, !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.return
    }
    // PLANE-LABEL: llvm.func @unproven_handle
    // PLANE: llvm.icmp
    // PLANE: llvm.call @obelisk_rt_v1_native_handle_offset
    // PLANE: llvm.select
    // PLANE: llvm.return
  }
}
