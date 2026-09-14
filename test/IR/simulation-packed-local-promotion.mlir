// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(canonicalize,mem2reg,canonicalize)))' | FileCheck %s

// Overlapping packed views must share one reaching definition. Preserve the
// X bits that no statement writes; no independent-slot SROA is valid here.
module {
  obelisk_sim.design @locals {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "overlap"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "branch"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "escape"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "packed"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "reentry"
    // CHECK-LABEL: obelisk_sim.func @overlap
    // CHECK-NOT: obelisk_sim.ref.
    // CHECK: %[[VALUE:.*]] = obelisk_sim.logic.constant 47 : i16, -4096 : i16
    // CHECK: obelisk_sim.return %[[VALUE]]
    obelisk_sim.func @overlap(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> !obelisk_sim.logic<16> attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %x = obelisk_sim.logic.constant 0 : i16, -1 : i16 : !obelisk_sim.logic<16>
      %local = obelisk_sim.ref.alloc %x : !obelisk_sim.logic<16> -> !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %low = obelisk_sim.ref.extract %local from 0 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %cross = obelisk_sim.ref.extract %local from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %ones = obelisk_sim.logic.constant -1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %two = obelisk_sim.logic.constant 2 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %ones to %low : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %two to %cross : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %value = obelisk_sim.ref.load %local : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.logic<16>
      obelisk_sim.return %value : !obelisk_sim.logic<16>
    }

    // CHECK-LABEL: obelisk_sim.func @branch
    // CHECK-NOT: obelisk_sim.ref.
    // CHECK: cf.cond_br
    // CHECK: obelisk_sim.return
    obelisk_sim.func @branch(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %condition: i1 {obelisk_sim.capture_kind = 2 : i32}, %a: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}, %b: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %x = obelisk_sim.logic.constant 0 : i16, -1 : i16 : !obelisk_sim.logic<16>
      %local = obelisk_sim.ref.alloc %x : !obelisk_sim.logic<16> -> !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %view = obelisk_sim.ref.extract %local from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      cf.cond_br %condition, ^left, ^right
    ^left:
      obelisk_sim.ref.store %a to %view : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      cf.br ^join
    ^right:
      obelisk_sim.ref.store %b to %view : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      cf.br ^join
    ^join:
      %value = obelisk_sim.ref.load %view : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %value : !obelisk_sim.logic<8>
    }

    // A ref argument can escape or be observed by its callee.
    // CHECK-LABEL: obelisk_sim.func @escape
    // CHECK: obelisk_sim.ref.alloc
    // CHECK: obelisk_sim.ref.extract
    // CHECK: obelisk_sim.ref.store
    obelisk_sim.func @escape(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %x = obelisk_sim.logic.constant 0 : i16, -1 : i16 : !obelisk_sim.logic<16>
      %local = obelisk_sim.ref.alloc %x : !obelisk_sim.logic<16> -> !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %view = obelisk_sim.ref.extract %local from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %value to %view : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.call @consume(%ctx, %local) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<16>>) -> ()
      obelisk_sim.return
    }
    obelisk_sim.func private @consume(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %reference: !obelisk_sim.ref<!obelisk_sim.logic<16>> {obelisk_sim.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32}

    // Source-language packed arrays use the same logical slot, despite their
    // non-scalar types and nonzero declared slice bounds.
    // CHECK-LABEL: obelisk_sim.func @packed
    // CHECK-NOT: obelisk_sim.ref.
    // CHECK: %[[PART:.*]] = obelisk_sim.logic.constant -85 : i8, 0 : i8
    // CHECK: %[[DEFAULT:.*]] = obelisk_sim.aggregate.default
    // CHECK: %[[BITS:.*]] = obelisk_sim.packed.flatten %[[DEFAULT]]
    // CHECK: %[[INSERT:.*]] = obelisk_sim.logic.insert %[[PART]] into %[[BITS]] at 4
    // CHECK: obelisk_sim.return %[[INSERT]]
    obelisk_sim.func @packed(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> !obelisk_sim.logic<16> attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %x = obelisk_sim.aggregate.default : !obelisk_sim.packed_array<15 : 0 x !obelisk_sim.logic<1>>
      %local = obelisk_sim.ref.alloc %x : !obelisk_sim.packed_array<15 : 0 x !obelisk_sim.logic<1>> -> !obelisk_sim.ref<!obelisk_sim.packed_array<15 : 0 x !obelisk_sim.logic<1>>>
      %view = obelisk_sim.ref.extract %local from 4 : !obelisk_sim.ref<!obelisk_sim.packed_array<15 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.ref<!obelisk_sim.packed_array<11 : 4 x !obelisk_sim.logic<1>>>
      %bits = obelisk_sim.logic.constant 171 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %part = obelisk_sim.packed.unflatten %bits : (!obelisk_sim.logic<8>) -> !obelisk_sim.packed_array<11 : 4 x !obelisk_sim.logic<1>>
      obelisk_sim.ref.store %part to %view : !obelisk_sim.packed_array<11 : 4 x !obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.packed_array<11 : 4 x !obelisk_sim.logic<1>>>
      %value = obelisk_sim.ref.load %local : !obelisk_sim.ref<!obelisk_sim.packed_array<15 : 0 x !obelisk_sim.logic<1>>> -> !obelisk_sim.packed_array<15 : 0 x !obelisk_sim.logic<1>>
      %result = obelisk_sim.packed.flatten %value : (!obelisk_sim.packed_array<15 : 0 x !obelisk_sim.logic<1>>) -> !obelisk_sim.logic<16>
      obelisk_sim.return %result : !obelisk_sim.logic<16>
    }

    // A cyclic allocation's initializer belongs to EACH execution. A partial
    // store does not replace a full initializer; retain the mem2reg guard.
    // CHECK-LABEL: obelisk_sim.func @reentry
    // CHECK: obelisk_sim.ref.alloc
    // CHECK: obelisk_sim.ref.extract
    // CHECK: obelisk_sim.ref.store
    obelisk_sim.func @reentry(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %again: i1 {obelisk_sim.capture_kind = 2 : i32}, %part: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<16> attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      cf.br ^loop
    ^loop:
      %x = obelisk_sim.logic.constant 0 : i16, -1 : i16 : !obelisk_sim.logic<16>
      %local = obelisk_sim.ref.alloc %x : !obelisk_sim.logic<16> -> !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %view = obelisk_sim.ref.extract %local from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %part to %view : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %value = obelisk_sim.ref.load %local : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.logic<16>
      cf.cond_br %again, ^loop, ^done
    ^done:
      obelisk_sim.return %value : !obelisk_sim.logic<16>
    }
  }
}
