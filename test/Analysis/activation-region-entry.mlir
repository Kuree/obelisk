// RUN: obelisk-opt %s --test-obelisk-activation-regions -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed

module {
  simulation.design @entry_boundary {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.unpacked_array<0 : 1 x i8> design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 1 x i8> design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "fresh_snapshot"
    // Initial and resume each take a fresh snapshot. Copied initial loads
    // must leave the resume view before normalization can merge equal loads.
    // The source CFG and its executable induction loop remain unchanged.
    // CHECK: regions @fresh_snapshot valid=1
    // CHECK-NEXT: node simulation.ref.load read:s0[0,16)
    // CHECK-NEXT: node simulation.ref.load read:s2[0,8)
    // CHECK-NEXT: node simulation.ref.store write:s3[0,8)
    // CHECK-NEXT: node scf.while write:s1[0,16)
    simulation.func @fresh_snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %source = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>
      %scalar = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %scalar_out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 2 : i32
      %first = simulation.ref.load %source : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>> -> !simulation.unpacked_array<0 : 1 x i8>
      %initial_scalar = simulation.ref.load %scalar : !simulation.ref<i8> -> i8
      cf.br ^start(%first, %initial_scalar : !simulation.unpacked_array<0 : 1 x i8>, i8)
    ^resume:
      %next = simulation.ref.load %source : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>> -> !simulation.unpacked_array<0 : 1 x i8>
      %resumed_scalar = simulation.ref.load %scalar : !simulation.ref<i8> -> i8
      cf.br ^start(%next, %resumed_scalar : !simulation.unpacked_array<0 : 1 x i8>, i8)
    ^start(%snapshot: !simulation.unpacked_array<0 : 1 x i8>, %value: i8):
      simulation.ref.store %value to %scalar_out : i8, !simulation.ref<i8>
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^copy, ^wait
    ^copy:
      %element = simulation.array.extract_dynamic %snapshot[%i] : (!simulation.unpacked_array<0 : 1 x i8>, i32) -> i8
      %reference = simulation.ref.array_element %out[%i] : (!simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>, i32) -> !simulation.ref<i8>
      simulation.ref.store %element to %reference : i8, !simulation.ref<i8>
      %following = arith.addi %i, %one : i32
      cf.br ^loop(%following : i32)
    ^wait:
      simulation.suspend.any %source, %scalar edges [0, 0] to ^resume : !simulation.ref<!simulation.unpacked_array<0 : 1 x i8>>, !simulation.ref<i8>
    }

    // A one-time store cannot be silently repeated on every activation.
    // CHECK: regions @initial_only_store valid=0 reason=initial/resume equivalence
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "initial_only_store"
    simulation.func @initial_only_store(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %source = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      simulation.ref.store %zero to %source : i8, !simulation.ref<i8>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %source : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %source to ^body : !simulation.ref<i8>
    }
    // Block layout need not follow dominance. The exit uses an operation
    // result whose defining loop header is printed later in the function.
    // CHECK: regions @forward_definition valid=1
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "forward_definition"
    simulation.func @forward_definition(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %trigger = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %limit = arith.constant 2 : i8
      cf.br ^header(%zero : i8)
    ^exit:
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %trigger to ^resume : !simulation.ref<i8>
    ^resume:
      cf.br ^header(%zero : i8)
    ^header(%i: i8):
      %result = arith.addi %i, %one : i8
      %more = arith.cmpi slt, %i, %limit : i8
      cf.cond_br %more, ^step, ^exit
    ^step:
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      cf.br ^header(%result : i8)
    }
    // Equal operation inventories do not prove equal operand dependencies.
    // CHECK: regions @different_wiring valid=0 reason=initial/resume equivalence
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "different_wiring"
    simulation.func @different_wiring(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %a = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %b = simulation.context.storage %ctx[4] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %first_a = simulation.ref.load %a : !simulation.ref<i8> -> i8
      %first_b = simulation.ref.load %b : !simulation.ref<i8> -> i8
      %first = arith.subi %first_a, %first_b : i8
      cf.br ^body(%first : i8)
    ^resume:
      %later_a = simulation.ref.load %a : !simulation.ref<i8> -> i8
      %later_b = simulation.ref.load %b : !simulation.ref<i8> -> i8
      %later = arith.subi %later_b, %later_a : i8
      cf.br ^body(%later : i8)
    ^body(%value: i8):
      simulation.ref.store %value to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %a, %b edges [0, 0] to ^resume : !simulation.ref<i8>, !simulation.ref<i8>
    }
  }
}
