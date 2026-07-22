// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s

!record = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "logic", type = !obelisk_sim.logic<8>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = i8, ordinal = 1, packedOffset = 0>
]>

module {
  obelisk_sim.design @aggregate_domain {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.aggregate_domain.rules.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @rules(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %bits = arith.constant 1 : i8
      %known = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %record = obelisk_sim.aggregate.construct %known, %bits : (!obelisk_sim.logic<8>, i8) -> !record
      %default = obelisk_sim.aggregate.default : !record
      %field = obelisk_sim.aggregate.extract %record[0] : (!record) -> !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: state-domain @aggregate_domain
// CHECK-LABEL: func @rules
// CHECK: bb0.op{{[0-9]+}}.result0: two-state (logic-from-bits)
// CHECK-NEXT: bb0.op{{[0-9]+}}.result0: may-four-state (unsupported-producer)
// CHECK-NEXT: bb0.op{{[0-9]+}}.result0: may-four-state (unsupported-producer)
// CHECK-NEXT: bb0.op{{[0-9]+}}.result0: may-four-state (unsupported-producer)
