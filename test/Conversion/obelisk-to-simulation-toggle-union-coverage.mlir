// RUN: obelisk-opt %s --split-input-file --obelisk-sim-prepare-coverage | FileCheck %s
// RUN: obelisk-opt %s --split-input-file --obelisk-sim-prepare-coverage \
// RUN:   | %python %S/Inputs/dump-coverage-schema.py \
// RUN:   | FileCheck %s --check-prefix=DIMS

!mixed = !obelisk_sim.packed_struct<[
  #obelisk_sim.field<name = "two_state", type = i1, ordinal = 0, packedOffset = 1>,
  #obelisk_sim.field<name = "four_state", type = !obelisk_sim.logic<1>, ordinal = 1, packedOffset = 0>
]>

module attributes {obelisk.coverage.metrics = ["toggle"]} {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    obelisk_sim.storage.decl 0 in 1 : !mixed design hierarchy "top.mixed" debug "mixed" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"union.sv", 1, 3, "union.sv", 1, 8, "">
    }
  }
}

// A packed aggregate with any four-state member is one four-state vector.
// CHECK: module attributes {
// CHECK-SAME: obelisk.coverage.toggle_bit_count = 2 : i64
// CHECK-SAME: obelisk.coverage.toggle_initial_unknown = array<i8: 3>
// CHECK-SAME: obelisk.coverage.toggle_initial_value = array<i8: 0>
// CHECK: obelisk_sim.storage.decl 0 {{.*}}obelisk.coverage.toggle_bindings = [{{.*}}low = 0 : i64, width = 2 : i64{{.*}}]
// DIMS-LABEL: schema 0
// DIMS: dimension kind=PackedStruct offset=0 width=2

// -----

!tagged = !obelisk_sim.packed_union<fields = [
  #obelisk_sim.field<name = "flag", type = !obelisk_sim.logic<1>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 1, packedOffset = 0>
], isTagged = true, tagBits = 1>

module attributes {obelisk.coverage.metrics = ["toggle"]} {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    obelisk_sim.storage.decl 0 in 1 : !tagged design hierarchy "top.tagged" debug "tagged" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"union.sv", 2, 3, "union.sv", 2, 9, "">
    }
  }
}

// The tag is an obligation at the MSB and shares the packed union's
// vector-wide four-state semantics.
// CHECK: module attributes {
// CHECK-SAME: obelisk.coverage.toggle_bit_count = 9 : i64
// CHECK-SAME: obelisk.coverage.toggle_initial_unknown = array<i8: -1, 1>
// CHECK-SAME: obelisk.coverage.toggle_initial_value = array<i8: 0, 0>
// CHECK: obelisk_sim.storage.decl 0 {{.*}}obelisk.coverage.toggle_bindings = [{{.*}}low = 0 : i64, width = 9 : i64{{.*}}]
// DIMS-LABEL: schema 1
// DIMS: dimension kind=PackedUnion offset=0 width=9
// DIMS: dimension kind=Tag offset=8 width=1

// -----

!tagged = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "flag", type = !obelisk_sim.logic<1>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "byte", type = i8, ordinal = 1, packedOffset = 0>
], isTagged = true>

module attributes {obelisk.coverage.metrics = ["toggle"]} {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    obelisk_sim.storage.decl 0 in 1 : !tagged design hierarchy "top.tagged" debug "tagged" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"union.sv", 3, 3, "union.sv", 3, 9, "">
    }
  }
}

// Unpacked tagged unions use disjoint member storage followed by the tag.
// CHECK: module attributes {
// CHECK-SAME: obelisk.coverage.toggle_bit_count = 11 : i64
// CHECK-SAME: obelisk.coverage.toggle_initial_unknown = array<i8: 1, 6>
// CHECK-SAME: obelisk.coverage.toggle_initial_value = array<i8: 0, 0>
// CHECK: obelisk_sim.storage.decl 0 {{.*}}obelisk.coverage.toggle_bindings = [{{.*}}low = 0 : i64, width = 11 : i64{{.*}}]
// DIMS-LABEL: schema 2
// DIMS: dimension kind=UnpackedUnion offset=0 width=11
// DIMS: dimension kind=Field offset=0 width=1
// DIMS: dimension kind=Field offset=1 width=8
// DIMS: dimension kind=Tag offset=9 width=2

// -----

!leading = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "padding", type = f64, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "flag", type = i1, ordinal = 1, packedOffset = 0>
]>
!overlap = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "leading", type = !leading, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "direct", type = i1, ordinal = 1, packedOffset = 0>
], isTagged = false>

module attributes {obelisk.coverage.metrics = ["toggle"]} {
  obelisk_sim.design @coverage {
    obelisk_sim.scope.decl 0
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    obelisk_sim.storage.decl 0 in 1 : !overlap design hierarchy "top.overlap" debug "overlap" {
      obelisk.coverage.source_authored,
      source_range = !obelisk.source_range<"union.sv", 4, 3, "union.sv", 4, 10, "">
    }
  }
}

// Dense obligations follow physical provenance, not declaration order: the
// direct bit is state bit 0 and the bit after the real member is state bit 64.
// CHECK: module attributes {
// CHECK-SAME: obelisk.coverage.toggle_bit_count = 2 : i64
// CHECK-SAME: obelisk.coverage.toggle_initial_unknown = array<i8: 0>
// CHECK-SAME: obelisk.coverage.toggle_initial_value = array<i8: 0>
// CHECK: obelisk_sim.storage.decl 0 {{.*}}obelisk.coverage.toggle_bindings = [{{.*}}low = 0 : i64, width = 1 : i64{{.*}}, {{.*}}low = 64 : i64, width = 1 : i64{{.*}}]
// DIMS-LABEL: schema 3
// DIMS: dimension kind=UnpackedUnion offset=0 width=2
// DIMS: dimension kind=Field offset=1 width=1
// DIMS: dimension kind=UnpackedStruct offset=1 width=1
// DIMS: dimension kind=Field offset=1 width=1
// DIMS: dimension kind=Scalar offset=1 width=1
// DIMS: dimension kind=Field offset=0 width=1
// DIMS: dimension kind=Scalar offset=0 width=1
