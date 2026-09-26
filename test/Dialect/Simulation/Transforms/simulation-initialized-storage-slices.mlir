// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-propagate-initialized-storage))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-propagate-initialized-storage{vpi=read}))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-propagate-initialized-storage{vpi=full}))' | FileCheck %s --check-prefix=FULL

// IEEE 1800-2023 6.8, 11.5.1: initialization precedes process startup,
// including after inlining. Fixed read-only aliases preserve both X/Z planes.
module {
  obelisk_sim.design @inlined {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "reader"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<16> design
    obelisk_sim.storage.decl 1 in 0 : i16 design
    // CHECK-LABEL: obelisk_sim.func @root
    // CHECK-COUNT-2: obelisk_sim.ref.store
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %bits = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i16>
      %value = obelisk_sim.logic.constant 4660 : i16, 240 : i16 : !obelisk_sim.logic<16>
      %integer = arith.constant 4660 : i16
      obelisk_sim.ref.store %value to %ref : !obelisk_sim.logic<16>, !obelisk_sim.ref<!obelisk_sim.logic<16>>
      obelisk_sim.ref.store %integer to %bits : i16, !obelisk_sim.ref<i16>
      %process = obelisk_sim.spawn @reader(%ctx, %ref, %bits) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<16>>, !obelisk_sim.ref<i16> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @reader
    // CHECK: obelisk_sim.logic.constant 3 : i4, -1 : i4
    // CHECK: arith.constant 35 : i8
    // CHECK-NOT: obelisk_sim.ref.load
    // CHECK: obelisk_sim.return
    // FULL-LABEL: obelisk_sim.func @reader
    // FULL-COUNT-2: obelisk_sim.ref.load
    obelisk_sim.func @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<16>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %bits: !obelisk_sim.ref<i16> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %byte = obelisk_sim.ref.extract %ref from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %nibble = obelisk_sim.ref.extract %byte from 0 : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %integer_byte = obelisk_sim.ref.extract %bits from 4 : !obelisk_sim.ref<i16> -> !obelisk_sim.ref<i8>
      %a = obelisk_sim.ref.load %nibble : !obelisk_sim.ref<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %b = obelisk_sim.ref.load %integer_byte : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return
    }
  }
  obelisk_sim.design @partial_write {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "reader"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<16> design
    obelisk_sim.storage.decl 1 in 0 : i16 design
    // CHECK-LABEL: obelisk_sim.func @root
    // CHECK-COUNT-2: obelisk_sim.ref.store
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %bits = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i16>
      %value = obelisk_sim.logic.constant 4660 : i16, 240 : i16 : !obelisk_sim.logic<16>
      %integer = arith.constant 4660 : i16
      obelisk_sim.ref.store %value to %ref : !obelisk_sim.logic<16>, !obelisk_sim.ref<!obelisk_sim.logic<16>>
      obelisk_sim.ref.store %integer to %bits : i16, !obelisk_sim.ref<i16>
      %process = obelisk_sim.spawn @reader(%ctx, %ref, %bits) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<16>>, !obelisk_sim.ref<i16> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @reader
    // CHECK: obelisk_sim.ref.load
    // CHECK: obelisk_sim.return
    // FULL-LABEL: obelisk_sim.func @reader
    // FULL-COUNT-2: obelisk_sim.ref.load
    obelisk_sim.func @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<16>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %bits: !obelisk_sim.ref<i16> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %byte = obelisk_sim.ref.extract %ref from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %nibble = obelisk_sim.ref.extract %byte from 0 : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %integer_byte = obelisk_sim.ref.extract %bits from 4 : !obelisk_sim.ref<i16> -> !obelisk_sim.ref<i8>
      %zero = obelisk_sim.logic.constant 0 : i4, 0 : i4 : !obelisk_sim.logic<4>
      obelisk_sim.ref.store %zero to %nibble : !obelisk_sim.logic<4>, !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %a = obelisk_sim.ref.load %nibble : !obelisk_sim.ref<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %b = obelisk_sim.ref.load %integer_byte : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return
    }
  }
  obelisk_sim.design @escaped {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "reader"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<16> design
    obelisk_sim.storage.decl 1 in 0 : i16 design
    // CHECK-LABEL: obelisk_sim.func @root
    // CHECK-COUNT-2: obelisk_sim.ref.store
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %bits = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i16>
      %value = obelisk_sim.logic.constant 4660 : i16, 240 : i16 : !obelisk_sim.logic<16>
      %integer = arith.constant 4660 : i16
      obelisk_sim.ref.store %value to %ref : !obelisk_sim.logic<16>, !obelisk_sim.ref<!obelisk_sim.logic<16>>
      obelisk_sim.ref.store %integer to %bits : i16, !obelisk_sim.ref<i16>
      %process = obelisk_sim.spawn @reader(%ctx, %ref, %bits) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<16>>, !obelisk_sim.ref<i16> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @reader
    // CHECK: obelisk_sim.ref.load
    // CHECK: obelisk_sim.return
    // FULL-LABEL: obelisk_sim.func @reader
    // FULL-COUNT-2: obelisk_sim.ref.load
    obelisk_sim.func @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<16>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %bits: !obelisk_sim.ref<i16> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %byte = obelisk_sim.ref.extract %ref from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %nibble = obelisk_sim.ref.extract %byte from 0 : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %integer_byte = obelisk_sim.ref.extract %bits from 4 : !obelisk_sim.ref<i16> -> !obelisk_sim.ref<i8>
      obelisk_sim.call @opaque(%ctx, %nibble) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<4>>) -> ()
      %a = obelisk_sim.ref.load %nibble : !obelisk_sim.ref<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %b = obelisk_sim.ref.load %integer_byte : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return
    }
    obelisk_sim.func private @opaque(!obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, !obelisk_sim.ref<!obelisk_sim.logic<4>> {obelisk_sim.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32}
  }
  obelisk_sim.design @writable {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "reader"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<16> design {observability = 2 : i32}
    obelisk_sim.storage.decl 1 in 0 : i16 design
    // CHECK-LABEL: obelisk_sim.func @root
    // CHECK-COUNT-2: obelisk_sim.ref.store
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %bits = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i16>
      %value = obelisk_sim.logic.constant 4660 : i16, 240 : i16 : !obelisk_sim.logic<16>
      %integer = arith.constant 4660 : i16
      obelisk_sim.ref.store %value to %ref : !obelisk_sim.logic<16>, !obelisk_sim.ref<!obelisk_sim.logic<16>>
      obelisk_sim.ref.store %integer to %bits : i16, !obelisk_sim.ref<i16>
      %process = obelisk_sim.spawn @reader(%ctx, %ref, %bits) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<16>>, !obelisk_sim.ref<i16> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @reader
    // CHECK: obelisk_sim.ref.load
    // CHECK: obelisk_sim.return
    // FULL-LABEL: obelisk_sim.func @reader
    // FULL-COUNT-2: obelisk_sim.ref.load
    obelisk_sim.func @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<16>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %bits: !obelisk_sim.ref<i16> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %byte = obelisk_sim.ref.extract %ref from 4 : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %nibble = obelisk_sim.ref.extract %byte from 0 : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %integer_byte = obelisk_sim.ref.extract %bits from 4 : !obelisk_sim.ref<i16> -> !obelisk_sim.ref<i8>
      %a = obelisk_sim.ref.load %nibble : !obelisk_sim.ref<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %b = obelisk_sim.ref.load %integer_byte : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.return
    }
  }
}
