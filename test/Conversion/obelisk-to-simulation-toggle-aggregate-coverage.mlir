// RUN: obelisk-opt %s --obelisk-sim-prepare-coverage | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-prepare-coverage \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s --check-prefix=DIMENSIONS

!memory = !obelisk_sim.unpacked_array<3 : 1 x !obelisk_sim.packed_array<0 : 7 x !obelisk_sim.logic<1>>>
!mixed = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "a", type = i2, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "skip", type = f64, ordinal = 1, packedOffset = 0>,
  #obelisk_sim.field<name = "b", type = !obelisk_sim.logic<3>, ordinal = 2, packedOffset = 0>
]>

module attributes {obelisk.coverage.metrics = ["toggle"]} {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    obelisk_sim.storage.decl 0 in 1 : !memory design hierarchy "top.mem" debug "mem" {
      obelisk.coverage.source_authored,
      obelisk.coverage.source_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<0 : 7 x !obelisk.enum<"memory_state_t", !obelisk.integral<1, false, true, 0 : 0, logic>>>>,
      source_range = !obelisk.source_range<"aggregate.sv", 1, 3, "aggregate.sv", 1, 6, "">
    }
    obelisk_sim.storage.decl 1 in 1 : !mixed design hierarchy "top.mixed" debug "mixed" {
      obelisk.coverage.source_authored,
      obelisk.coverage.source_type = !obelisk.source_aggregate<"mixed_t", false, false, false, false, false, false, 0, 69, 69, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<2, false, false, 1 : 0, bit>}, {name = "skip", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.real}, {name = "b", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.enum<"member_state_t", !obelisk.integral<3, false, true, 2 : 0, logic>>}]>,
      source_range = !obelisk.source_range<"aggregate.sv", 2, 3, "aggregate.sv", 2, 8, "">
    }
    obelisk_sim.storage.decl 2 in 1 : !obelisk_sim.logic<2> design hierarchy "top.state" debug "state" {
      obelisk.coverage.source_authored,
      obelisk.coverage.source_type = !obelisk.enum<"state_t", !obelisk.integral<2, false, false, 1 : 0, bit>>,
      source_range = !obelisk.source_range<"aggregate.sv", 3, 3, "aggregate.sv", 3, 8, "">
    }
  }
}

// Fixed unpacked arrays are obligations, while non-integral aggregate leaves
// and ABI padding are not.
// CHECK: module attributes {
// CHECK-SAME: obelisk.coverage.toggle_bit_count = 31 : i64
// CHECK-SAME: obelisk.coverage.toggle_initial_unknown = array<i8: -4, -1, -1, 127>
// CHECK-SAME: obelisk.coverage.toggle_initial_value = array<i8: 0, 0, 0, 0>
// CHECK-DAG: obelisk_sim.storage.decl 0 {{.*}}obelisk.coverage.toggle_bindings = [{{.*}}width = 24 : i64{{.*}}]{{.*}}obelisk.coverage.toggle_observable
// CHECK-DAG: obelisk_sim.storage.decl 1 {{.*}}obelisk.coverage.toggle_bindings = [{{.*}}width = 2 : i64{{.*}}, {{.*}}width = 3 : i64{{.*}}]{{.*}}obelisk.coverage.toggle_observable
// CHECK-DAG: obelisk_sim.storage.decl 2 {{.*}}obelisk.coverage.toggle_bindings = [{{.*}}width = 2 : i64{{.*}}]{{.*}}obelisk.coverage.toggle_observable
// CHECK-NOT: obelisk.coverage.source_type
// Enum typedef identity must survive below fixed arrays and aggregate fields,
// as well as at the object root.
// DIMENSIONS-COUNT-3: dimension kind=Enum
