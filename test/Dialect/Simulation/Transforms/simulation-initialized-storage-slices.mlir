// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-propagate-initialized-storage))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-propagate-initialized-storage{vpi=read}))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-propagate-initialized-storage{vpi=full}))' | FileCheck %s --check-prefix=FULL

// IEEE 1800-2023 6.8, 11.5.1: initialization precedes process startup,
// including after inlining. Fixed read-only aliases preserve both X/Z planes.
module {
  simulation.design @inlined {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "reader"
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design
    simulation.storage.decl 1 in 0 : i16 design
    // CHECK-LABEL: simulation.func @root
    // CHECK-COUNT-2: simulation.ref.store
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<16>>
      %bits = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %value = simulation.logic.constant 4660 : i16, 240 : i16 : !simulation.logic<16>
      %integer = arith.constant 4660 : i16
      simulation.ref.store %value to %ref : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      simulation.ref.store %integer to %bits : i16, !simulation.ref<i16>
      %process = simulation.spawn @reader(%ctx, %ref, %bits) : !simulation.context, !simulation.ref<!simulation.logic<16>>, !simulation.ref<i16> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func @reader
    // CHECK: simulation.logic.constant 3 : i4, -1 : i4
    // CHECK: arith.constant 35 : i8
    // CHECK-NOT: simulation.ref.load
    // CHECK: simulation.return
    // FULL-LABEL: simulation.func @reader
    // FULL-COUNT-2: simulation.ref.load
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %bits: !simulation.ref<i16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %byte = simulation.ref.extract %ref from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %nibble = simulation.ref.extract %byte from 0 : !simulation.ref<!simulation.logic<8>> -> !simulation.ref<!simulation.logic<4>>
      %integer_byte = simulation.ref.extract %bits from 4 : !simulation.ref<i16> -> !simulation.ref<i8>
      %a = simulation.ref.load %nibble : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %b = simulation.ref.load %integer_byte : !simulation.ref<i8> -> i8
      simulation.return
    }
  }
  simulation.design @partial_write {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "reader"
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design
    simulation.storage.decl 1 in 0 : i16 design
    // CHECK-LABEL: simulation.func @root
    // CHECK-COUNT-2: simulation.ref.store
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<16>>
      %bits = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %value = simulation.logic.constant 4660 : i16, 240 : i16 : !simulation.logic<16>
      %integer = arith.constant 4660 : i16
      simulation.ref.store %value to %ref : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      simulation.ref.store %integer to %bits : i16, !simulation.ref<i16>
      %process = simulation.spawn @reader(%ctx, %ref, %bits) : !simulation.context, !simulation.ref<!simulation.logic<16>>, !simulation.ref<i16> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func @reader
    // CHECK: simulation.ref.load
    // CHECK: simulation.return
    // FULL-LABEL: simulation.func @reader
    // FULL-COUNT-2: simulation.ref.load
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %bits: !simulation.ref<i16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %byte = simulation.ref.extract %ref from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %nibble = simulation.ref.extract %byte from 0 : !simulation.ref<!simulation.logic<8>> -> !simulation.ref<!simulation.logic<4>>
      %integer_byte = simulation.ref.extract %bits from 4 : !simulation.ref<i16> -> !simulation.ref<i8>
      %zero = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %zero to %nibble : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %a = simulation.ref.load %nibble : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %b = simulation.ref.load %integer_byte : !simulation.ref<i8> -> i8
      simulation.return
    }
  }
  simulation.design @escaped {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "reader"
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design
    simulation.storage.decl 1 in 0 : i16 design
    // CHECK-LABEL: simulation.func @root
    // CHECK-COUNT-2: simulation.ref.store
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<16>>
      %bits = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %value = simulation.logic.constant 4660 : i16, 240 : i16 : !simulation.logic<16>
      %integer = arith.constant 4660 : i16
      simulation.ref.store %value to %ref : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      simulation.ref.store %integer to %bits : i16, !simulation.ref<i16>
      %process = simulation.spawn @reader(%ctx, %ref, %bits) : !simulation.context, !simulation.ref<!simulation.logic<16>>, !simulation.ref<i16> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func @reader
    // CHECK: simulation.ref.load
    // CHECK: simulation.return
    // FULL-LABEL: simulation.func @reader
    // FULL-COUNT-2: simulation.ref.load
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %bits: !simulation.ref<i16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %byte = simulation.ref.extract %ref from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %nibble = simulation.ref.extract %byte from 0 : !simulation.ref<!simulation.logic<8>> -> !simulation.ref<!simulation.logic<4>>
      %integer_byte = simulation.ref.extract %bits from 4 : !simulation.ref<i16> -> !simulation.ref<i8>
      simulation.call @opaque(%ctx, %nibble) : (!simulation.context, !simulation.ref<!simulation.logic<4>>) -> ()
      %a = simulation.ref.load %nibble : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %b = simulation.ref.load %integer_byte : !simulation.ref<i8> -> i8
      simulation.return
    }
    simulation.func private @opaque(!simulation.context {simulation.capture_kind = 0 : i32}, !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32}
  }
  simulation.design @writable {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "reader"
    simulation.storage.decl 0 in 0 : !simulation.logic<16> design {observability = 2 : i32}
    simulation.storage.decl 1 in 0 : i16 design
    // CHECK-LABEL: simulation.func @root
    // CHECK-COUNT-2: simulation.ref.store
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<16>>
      %bits = simulation.context.storage %ctx[1] : !simulation.ref<i16>
      %value = simulation.logic.constant 4660 : i16, 240 : i16 : !simulation.logic<16>
      %integer = arith.constant 4660 : i16
      simulation.ref.store %value to %ref : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      simulation.ref.store %integer to %bits : i16, !simulation.ref<i16>
      %process = simulation.spawn @reader(%ctx, %ref, %bits) : !simulation.context, !simulation.ref<!simulation.logic<16>>, !simulation.ref<i16> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func @reader
    // CHECK: simulation.ref.load
    // CHECK: simulation.return
    // FULL-LABEL: simulation.func @reader
    // FULL-COUNT-2: simulation.ref.load
    simulation.func @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %bits: !simulation.ref<i16> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %byte = simulation.ref.extract %ref from 4 : !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %nibble = simulation.ref.extract %byte from 0 : !simulation.ref<!simulation.logic<8>> -> !simulation.ref<!simulation.logic<4>>
      %integer_byte = simulation.ref.extract %bits from 4 : !simulation.ref<i16> -> !simulation.ref<i8>
      %a = simulation.ref.load %nibble : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %b = simulation.ref.load %integer_byte : !simulation.ref<i8> -> i8
      simulation.return
    }
  }
}
