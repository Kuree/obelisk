// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s

// An X/Z index makes an array selection invalid, and an invalid read yields
// the element default (IEEE 1800-2023 7.4.6). A four-bit index addresses all
// sixteen elements for every known value, so it reads a known element once
// its own root is known: the copy target stays inductively two-state. A
// five-bit index can address past the declared range even when known, so its
// copy target is rejected.

!mem = !simulation.unpacked_array<0 : 15 x !simulation.logic<8>>

module {
  simulation.design @known_selector_bounds {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "known_selector_bounds.read"
    simulation.code_unit.decl 2 in 0 initial hierarchy "known_selector_bounds.fill"
    simulation.storage.decl 0 in 0 : !mem design
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<5> design
    simulation.storage.decl 4 in 0 : !simulation.logic<8> design
    simulation.func @read(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %mem: !simulation.ref<!mem> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %index: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %copy: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %wide: !simulation.ref<!simulation.logic<5>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
        %clipped: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %i = simulation.ref.load %index : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      %ix = simulation.logic.resize %i signed = false : !simulation.logic<4> -> !simulation.logic<65>
      %cell = simulation.ref.array_element %mem[%ix] : (!simulation.ref<!mem>, !simulation.logic<65>) -> !simulation.ref<!simulation.logic<8>>
      %v = simulation.ref.load %cell : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %v to %copy : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %w = simulation.ref.load %wide : !simulation.ref<!simulation.logic<5>> -> !simulation.logic<5>
      %wx = simulation.logic.resize %w signed = false : !simulation.logic<5> -> !simulation.logic<65>
      %far = simulation.ref.array_element %mem[%wx] : (!simulation.ref<!mem>, !simulation.logic<65>) -> !simulation.ref<!simulation.logic<8>>
      %u = simulation.ref.load %far : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %u to %clipped : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
    // Known writers: bits nothing writes would hold X/Z forever.
    simulation.func @fill(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %mem: !simulation.ref<!mem> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %index: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %wide: !simulation.ref<!simulation.logic<5>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %byte = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      %all = simulation.aggregate.splat %byte : (!simulation.logic<8>) -> !mem
      simulation.ref.store %all to %mem : !mem, !simulation.ref<!mem>
      %i = simulation.logic.constant 3 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %i to %index : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %w = simulation.logic.constant 3 : i5, 0 : i5 : !simulation.logic<5>
      simulation.ref.store %w to %wide : !simulation.logic<5>, !simulation.ref<!simulation.logic<5>>
      simulation.return
    }
  }
  // A readiness scan of a root wider than 64 Kbit would cost more than a
  // guarded activation recovers, so such a root is never assumed known even
  // when every write to it is known; reads through it stay four-state.
  simulation.design @unguarded_wide {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "unguarded_wide.read"
    simulation.code_unit.decl 2 in 0 initial hierarchy "unguarded_wide.fill"
    simulation.storage.decl 0 in 0 : !simulation.unpacked_array<0 : 4095 x !simulation.logic<32>> design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design
    simulation.func @read(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %mem: !simulation.ref<!simulation.unpacked_array<0 : 4095 x !simulation.logic<32>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %copy: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %cell = simulation.ref.subelement %mem[[7]] : !simulation.ref<!simulation.unpacked_array<0 : 4095 x !simulation.logic<32>>> -> !simulation.ref<!simulation.logic<32>>
      %v = simulation.ref.load %cell : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.ref.store %v to %copy : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      simulation.return
    }
    simulation.func @fill(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %mem: !simulation.ref<!simulation.unpacked_array<0 : 4095 x !simulation.logic<32>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %word = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      %all = simulation.aggregate.splat %word : (!simulation.logic<32>) -> !simulation.unpacked_array<0 : 4095 x !simulation.logic<32>>
      simulation.ref.store %all to %mem : !simulation.unpacked_array<0 : 4095 x !simulation.logic<32>>, !simulation.ref<!simulation.unpacked_array<0 : 4095 x !simulation.logic<32>>>
      simulation.return
    }
  }
}

// CHECK-LABEL: state-domain @known_selector_bounds
// CHECK-NEXT: root storage 0: inductive-two-state
// CHECK-NEXT: root storage 1: inductive-two-state
// CHECK-NEXT: root storage 2: inductive-two-state
// CHECK-NEXT: root storage 3: inductive-two-state
// CHECK-NEXT: func @fill
// CHECK: func @read
// Unconditionally, every load may still observe X/Z.
// CHECK-NEXT: bb0.op0.result0: may-four-state (ref-load)
// CHECK-NEXT: bb0.op1.result0: may-four-state (logic-resize)
// CHECK-NEXT: bb0.op3.result0: may-four-state (ref-load)

// CHECK-LABEL: state-domain @unguarded_wide
// CHECK-NOT: root storage
// CHECK: func @read
// CHECK-NEXT: bb0.op1.result0: may-four-state (ref-load)
