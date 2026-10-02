// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off}))' | FileCheck %s
// Equal array snapshots share reads, while storage and publications remain
// separate. A dynamic write invalidates the relation. Invalid/X indices use
// the same declared-range default in the SSA extract as in the source load.
!array = !simulation.unpacked_array<3 : 0 x !simulation.logic<8>>
module {
  simulation.design @replicas {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.storage.decl 1 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "equal"
    simulation.code_unit.decl 2 in 0 function hierarchy "consumer"
    // CHECK-LABEL: simulation.func @equal
    // CHECK: %[[SNAPSHOT:.*]] = simulation.ref.load
    // CHECK: simulation.ref.store %[[SNAPSHOT]]
    // CHECK: simulation.array.extract_dynamic %[[SNAPSHOT]]
    // CHECK: simulation.ref.store
    // CHECK: simulation.ref.load
    simulation.func @equal(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: !simulation.logic<8> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8> attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !simulation.ref<!array>
      %b = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %snapshot = simulation.ref.load %a : !simulation.ref<!array> -> !array
      simulation.ref.store %snapshot to %b : !array, !simulation.ref<!array>
      %ref = simulation.ref.array_element %b[%index] : (!simulation.ref<!array>, !simulation.logic<8>) -> !simulation.ref<!simulation.logic<8>>
      %first = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %first to %ref : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %second = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %second : !simulation.logic<8>
    }
    // A separate consumer prevents private-storage promotion. The equality
    // certificate above still shares preparation without sharing addresses.
    simulation.func @consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !array attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %b = simulation.context.storage %ctx[1] : !simulation.ref<!array>
      %value = simulation.ref.load %b : !simulation.ref<!array> -> !array
      simulation.return %value : !array
    }
  }
}
