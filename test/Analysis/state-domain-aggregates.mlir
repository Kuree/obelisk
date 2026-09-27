// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s

!record = !simulation.unpacked_struct<[
  #simulation.field<name = "logic", type = !simulation.logic<8>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = i8, ordinal = 1, packedOffset = 0>
]>

!packed = !simulation.packed_array<7 : 0 x !simulation.logic<1>>

module {
  simulation.design @aggregate_domain {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.aggregate_domain.rules.9000001"
    simulation.scope.decl 0
    simulation.func @rules(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %bits = arith.constant 1 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %record = simulation.aggregate.construct %known, %bits : (!simulation.logic<8>, i8) -> !record
      %default = simulation.aggregate.default : !record
      %field = simulation.aggregate.extract %record[0] : (!record) -> !simulation.logic<8>
      %packed = simulation.packed.unflatten %known : (!simulation.logic<8>) -> !packed
      %flattened = simulation.packed.flatten %packed : (!packed) -> !simulation.logic<8>
      simulation.return
    }
  }
}

// CHECK-LABEL: state-domain @aggregate_domain
// CHECK-LABEL: func @rules
// CHECK: bb0.op{{[0-9]+}}.result0: two-state (logic-from-bits)
// CHECK-NEXT: bb0.op{{[0-9]+}}.result0: may-four-state (unsupported-producer)
// CHECK-NEXT: bb0.op{{[0-9]+}}.result0: may-four-state (unsupported-producer)
// CHECK-NEXT: bb0.op{{[0-9]+}}.result0: may-four-state (unsupported-producer)
// CHECK-NEXT: bb0.op{{[0-9]+}}.result0: two-state (packed-view)
// CHECK-NEXT: bb0.op{{[0-9]+}}.result0: two-state (packed-view)
