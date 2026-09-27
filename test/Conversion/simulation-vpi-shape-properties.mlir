// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t.dump \
// RUN:   OBELISK_TEST_VPI=read %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s < %t.dump

!packed_one = !simulation.packed_array<0 : 0 x i1>
!unpacked_scalar = !simulation.unpacked_array<1 : 0 x i1>
!unpacked_vector = !simulation.unpacked_array<1 : 0 x i8>
!packed_record = !simulation.packed_struct<[
  #simulation.field<name = "bit", type = i1, ordinal = 0, packedOffset = 0>
]>
!unpacked_record = !simulation.unpacked_struct<[
  #simulation.field<name = "bit", type = i1, ordinal = 0, packedOffset = 0>
]>
!packed_choice = !simulation.packed_union<fields = [
  #simulation.field<name = "bit", type = i1, ordinal = 0, packedOffset = 0>
], isTagged = false>
!unpacked_choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "bit", type = i1, ordinal = 0, packedOffset = 0>
], isTagged = false>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @vpi_shape_properties {
    simulation.scope.decl 0 hierarchy "top"
    simulation.storage.decl 0 in 0 : i1 design hierarchy "top.basic_scalar"
    simulation.storage.decl 1 in 0 : i8 design hierarchy "top.basic_vector"
    simulation.storage.decl 2 in 0 : !packed_one design hierarchy "top.packed_array"
    simulation.storage.decl 3 in 0 : !packed_record design hierarchy "top.packed_struct"
    simulation.storage.decl 4 in 0 : !packed_choice design hierarchy "top.packed_union"
    simulation.storage.decl 5 in 0 : f64 design hierarchy "top.real"
    simulation.storage.decl 6 in 0 : !unpacked_scalar design hierarchy "top.unpacked_scalar"
    simulation.storage.decl 7 in 0 : !unpacked_record design hierarchy "top.unpacked_struct"
    simulation.storage.decl 8 in 0 : !unpacked_choice design hierarchy "top.unpacked_union"
    simulation.storage.decl 9 in 0 : !unpacked_vector design hierarchy "top.unpacked_vector"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// Basic packed one-bit and multi-bit types remain distinguishable by width.
// CHECK: object name=top.basic_scalar kind=2 vpi_kind=48 {{.*}} width=1 {{.*}} type_kind=1 type_flags=0x4
// CHECK: object name=top.basic_vector kind=2 vpi_kind=48 {{.*}} width=8 {{.*}} type_kind=1 type_flags=0x4

// Packed aggregates retain their aggregate kind even at a flattened width of
// one, so VPI can report vector rather than infer scalar from width alone.
// CHECK: object name=top.packed_array kind=2 vpi_kind=48 {{.*}} width=1 {{.*}} type_kind=2 type_flags=0x4
// CHECK: object name=top.packed_struct kind=2 vpi_kind=48 {{.*}} width=1 {{.*}} type_kind=3 type_flags=0x4
// CHECK: object name=top.packed_union kind=2 vpi_kind=48 {{.*}} width=1 {{.*}} type_kind=4 type_flags=0x4

// Reals and unpacked aggregates are non-packed. Unpacked arrays retain their
// element-type link, which the runtime follows recursively for the property.
// CHECK: object name=top.real kind=2 vpi_kind=48 {{.*}} width=64 {{.*}} type_kind=1 type_flags=0x0
// CHECK: object name=top.unpacked_scalar kind=2 vpi_kind=48 {{.*}} width=2 {{.*}} type_kind=2 type_flags=0x0 {{.*}} element_kind=1 element_flags=0x4 element_width=1
// CHECK: object name=top.unpacked_struct kind=2 vpi_kind=48 {{.*}} width=1 {{.*}} type_kind=3 type_flags=0x0
// CHECK: object name=top.unpacked_union kind=2 vpi_kind=48 {{.*}} width=1 {{.*}} type_kind=4 type_flags=0x0
// CHECK: object name=top.unpacked_vector kind=2 vpi_kind=48 {{.*}} width=16 {{.*}} type_kind=2 type_flags=0x0 {{.*}} element_kind=1 element_flags=0x4 element_width=8
