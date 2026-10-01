// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(canonicalize,mem2reg,canonicalize)))' | FileCheck %s

// Overlapping packed views must share one reaching definition. Preserve the
// X bits that no statement writes; no independent-slot SROA is valid here.
!mixed = !simulation.packed_union<fields = [#simulation.field<name = "four", type = !simulation.logic<16>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "two", type = i16, ordinal = 1, packedOffset = 0>], isTagged = false>
module {
  simulation.design @locals {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "overlap"
    simulation.code_unit.decl 2 in 0 function hierarchy "branch"
    simulation.code_unit.decl 3 in 0 function hierarchy "escape"
    simulation.code_unit.decl 4 in 0 function hierarchy "packed"
    simulation.code_unit.decl 5 in 0 function hierarchy "reentry"
    simulation.code_unit.decl 6 in 0 function hierarchy "nested_union"
    // CHECK-LABEL: simulation.func @overlap
    // CHECK-NOT: simulation.ref.
    // CHECK: %[[VALUE:.*]] = simulation.logic.constant 47 : i16, -4096 : i16
    // CHECK: simulation.return %[[VALUE]]
    simulation.func @overlap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !simulation.logic<16> attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %x = simulation.logic.constant 0 : i16, -1 : i16 : !simulation.logic<16>
      %local = simulation.ref.alloc %x : !simulation.logic<16> -> !simulation.ref<!simulation.logic<16>>
      %low = simulation.ref.extract %local from 0 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %cross = simulation.ref.extract %local from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %ones = simulation.logic.constant -1 : i8, 0 : i8 : !simulation.logic<8>
      %two = simulation.logic.constant 2 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %ones to %low : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %two to %cross : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %value = simulation.ref.load %local : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.return %value : !simulation.logic<16>
    }

    // CHECK-LABEL: simulation.func @branch
    // CHECK-NOT: simulation.ref.
    // CHECK: cf.cond_br
    // CHECK: simulation.return
    simulation.func @branch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}, %a: !simulation.logic<8> {simulation.capture_kind = 2 : i32}, %b: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %x = simulation.logic.constant 0 : i16, -1 : i16 : !simulation.logic<16>
      %local = simulation.ref.alloc %x : !simulation.logic<16> -> !simulation.ref<!simulation.logic<16>>
      %view = simulation.ref.extract %local from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      cf.cond_br %condition, ^left, ^right
    ^left:
      simulation.ref.store %a to %view : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^join
    ^right:
      simulation.ref.store %b to %view : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^join
    ^join:
      %value = simulation.ref.load %view : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %value : !simulation.logic<8>
    }

    // A ref argument can escape or be observed by its callee.
    // CHECK-LABEL: simulation.func @escape
    // CHECK: simulation.ref.alloc
    // CHECK: simulation.ref.extract
    // CHECK: simulation.ref.store
    simulation.func @escape(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %x = simulation.logic.constant 0 : i16, -1 : i16 : !simulation.logic<16>
      %local = simulation.ref.alloc %x : !simulation.logic<16> -> !simulation.ref<!simulation.logic<16>>
      %view = simulation.ref.extract %local from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %view : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.call @consume(%ctx, %local) : (!simulation.context, !simulation.ref<!simulation.logic<16>>) -> ()
      simulation.return
    }
    simulation.func private @consume(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %reference: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32}

    // Source-language packed arrays use the same logical slot, despite their
    // non-scalar types and nonzero declared slice bounds.
    // CHECK-LABEL: simulation.func @packed
    // CHECK-NOT: simulation.ref.
    // CHECK: %[[PART:.*]] = simulation.logic.constant -85 : i8, 0 : i8
    // CHECK: %[[DEFAULT:.*]] = simulation.aggregate.default
    // CHECK: %[[BITS:.*]] = simulation.packed.flatten %[[DEFAULT]]
    // CHECK: %[[INSERT:.*]] = simulation.logic.insert %[[PART]] into %[[BITS]] at 4
    // CHECK: simulation.return %[[INSERT]]
    simulation.func @packed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !simulation.logic<16> attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %x = simulation.aggregate.default : !simulation.packed_array<15 : 0 x !simulation.logic<1>>
      %local = simulation.ref.alloc %x : !simulation.packed_array<15 : 0 x !simulation.logic<1>> -> !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<1>>>
      %view = simulation.ref.extract %local from 4 : !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<1>>> -> !simulation.ref<!simulation.packed_array<11 : 4 x !simulation.logic<1>>>
      %bits = simulation.logic.constant 171 : i8, 0 : i8 : !simulation.logic<8>
      %part = simulation.packed.unflatten %bits : (!simulation.logic<8>) -> !simulation.packed_array<11 : 4 x !simulation.logic<1>>
      simulation.ref.store %part to %view : !simulation.packed_array<11 : 4 x !simulation.logic<1>>, !simulation.ref<!simulation.packed_array<11 : 4 x !simulation.logic<1>>>
      %value = simulation.ref.load %local : !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<1>>> -> !simulation.packed_array<15 : 0 x !simulation.logic<1>>
      %result = simulation.packed.flatten %value : (!simulation.packed_array<15 : 0 x !simulation.logic<1>>) -> !simulation.logic<16>
      simulation.return %result : !simulation.logic<16>
    }

    // LRM 6.21: canonicalization makes each cyclic allocation's initializer
    // explicit, so promotion preserves X in the bits outside the partial store.
    // CHECK-LABEL: simulation.func @reentry
    // CHECK-NOT: simulation.ref.
    // CHECK: %[[RESET:.*]] = simulation.logic.constant 0 : i16, -1 : i16
    // CHECK: %[[RESET_VALUE:.*]] = simulation.logic.insert %arg2 into %[[RESET]] at 4
    // CHECK: simulation.return %[[RESET_VALUE]]
    simulation.func @reentry(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %again: i1 {simulation.capture_kind = 2 : i32}, %part: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<16> attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      cf.br ^loop
    ^loop:
      %x = simulation.logic.constant 0 : i16, -1 : i16 : !simulation.logic<16>
      %local = simulation.ref.alloc %x : !simulation.logic<16> -> !simulation.ref<!simulation.logic<16>>
      %view = simulation.ref.extract %local from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %part to %view : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %value = simulation.ref.load %local : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      cf.cond_br %again, ^loop, ^done
    ^done:
      simulation.return %value : !simulation.logic<16>
    }

    // LRM 6.11.2, 7.3.1, 11.5.1: a union nested in a packed view still
    // shares one backing value. Convert X/Z only for its two-state read;
    // a partial write preserves every bit outside that selection.
    // CHECK-LABEL: simulation.func @nested_union
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.logic.extract {{.*}} from 4
    // CHECK: simulation.logic.to_bits
    // CHECK: simulation.logic.from_bits
    // CHECK: simulation.logic.insert {{.*}} at 8
    // CHECK: simulation.return
    simulation.func @nested_union(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %initial: !simulation.logic<24> {simulation.capture_kind = 2 : i32}, %part: i8 {simulation.capture_kind = 2 : i32}) -> (!simulation.logic<24>, i16) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %local = simulation.ref.alloc %initial : !simulation.logic<24> -> !simulation.ref<!simulation.logic<24>>
      %union = simulation.ref.extract %local from 4 : !simulation.ref<!simulation.logic<24>> -> !simulation.ref<!mixed>
      %two = simulation.ref.subelement %union[[1]] : !simulation.ref<!mixed> -> !simulation.ref<i16>
      %previous = simulation.ref.load %two : !simulation.ref<i16> -> i16
      %low = simulation.ref.extract %two from 4 : !simulation.ref<i16> -> !simulation.ref<i8>
      simulation.ref.store %part to %low : i8, !simulation.ref<i8>
      %result = simulation.ref.load %local : !simulation.ref<!simulation.logic<24>> -> !simulation.logic<24>
      simulation.return %result, %previous : !simulation.logic<24>, i16
    }
  }
}
