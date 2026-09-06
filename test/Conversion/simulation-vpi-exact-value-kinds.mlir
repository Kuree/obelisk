// RUN: env OBELISK_TEST_INPUT=%s OBELISK_TEST_OUTPUT=%t.dump \
// RUN:   OBELISK_TEST_VPI=read %obj_root/test/obelisk-design-database-dump-test \
// RUN:   --gtest_filter=GeneratedDesignDatabase.Dump
// RUN: FileCheck %s < %t.dump

!logic_vector = !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>
!packed_record = !obelisk_sim.packed_struct<[
  #obelisk_sim.field<name = "member", type = i1, ordinal = 0, packedOffset = 0>
]>
!packed_record_array = !obelisk_sim.packed_array<1 : 0 x !packed_record>
!packed_union = !obelisk_sim.packed_union<fields = [
  #obelisk_sim.field<name = "member", type = i1, ordinal = 0, packedOffset = 0>
], isTagged = false>
!packed_union_array = !obelisk_sim.packed_array<1 : 0 x !packed_union>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @vpi_exact_value_kinds {
    obelisk_sim.scope.decl 0 hierarchy "top"

    obelisk_sim.storage.decl 0 in 0 : i1 design hierarchy "top.bit_value" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
        isFourState = false, range = [0, 0], children = [], childNames = []>}
    obelisk_sim.storage.decl 1 in 0 : i32 design hierarchy "top.int_value" {
      vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = true,
        isFourState = false, range = [31, 0], children = [], childNames = []>}
    obelisk_sim.storage.decl 2 in 0 : f32 design hierarchy "top.shortreal_value" {
      vpi_type = #obelisk_sim.vpi_type<kind = shortreal, isSigned = false,
        isFourState = false, range = [], children = [], childNames = []>}
    obelisk_sim.storage.decl 3 in 0 : f64 design hierarchy "top.real_value" {
      vpi_type = #obelisk_sim.vpi_type<kind = real, isSigned = false,
        isFourState = false, range = [], children = [], childNames = []>}
    obelisk_sim.net.decl 0 in 0 : i1 design hierarchy "top.bit_net" {
      vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false,
        isFourState = false, range = [0, 0], children = [], childNames = []>}
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design hierarchy "top.logic_net" {
      vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false,
        isFourState = true, range = [0, 0], children = [], childNames = []>}
    obelisk_sim.storage.decl 4 in 0 : !logic_vector design hierarchy "top.logic_vector" {
      vpi_type = #obelisk_sim.vpi_type<kind = packed_array, isSigned = false,
        isFourState = true, range = [7, 0], children = [
          #obelisk_sim.vpi_type<kind = logic, isSigned = false,
            isFourState = true, range = [0, 0], children = [],
            childNames = []>], childNames = []>}
    obelisk_sim.storage.decl 5 in 0 : !packed_record_array design hierarchy "top.packed_record_value" {
      vpi_type = #obelisk_sim.vpi_type<kind = packed_array, isSigned = false,
        isFourState = false, range = [1, 0], children = [
          #obelisk_sim.vpi_type<kind = packed_struct, isSigned = false,
            isFourState = false, name = "record_t", range = [], children = [
              #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                isFourState = false, range = [0, 0], children = [],
                childNames = []>], childNames = ["member"], isTagged = false,
            isSoft = false, bitWidth = 1 : i64,
            selectableWidth = 1 : i64, bitstreamWidth = 1 : i64,
            tagBits = 0 : i64, childOrdinals = [0],
            childPackedOffsets = [0]>], childNames = []>}
    obelisk_sim.net.decl 2 in 0 : !packed_record_array design hierarchy "top.packed_record_net" {
      vpi_type = #obelisk_sim.vpi_type<kind = packed_array, isSigned = false,
        isFourState = false, range = [1, 0], children = [
          #obelisk_sim.vpi_type<kind = packed_struct, isSigned = false,
            isFourState = false, name = "record_t", range = [], children = [
              #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                isFourState = false, range = [0, 0], children = [],
                childNames = []>], childNames = ["member"], isTagged = false,
            isSoft = false, bitWidth = 1 : i64,
            selectableWidth = 1 : i64, bitstreamWidth = 1 : i64,
            tagBits = 0 : i64, childOrdinals = [0],
            childPackedOffsets = [0]>], childNames = []>}
    obelisk_sim.net.decl 3 in 0 : !packed_union_array design hierarchy "top.packed_union_net" {
      vpi_type = #obelisk_sim.vpi_type<kind = packed_array, isSigned = false,
        isFourState = false, range = [1, 0], children = [
          #obelisk_sim.vpi_type<kind = packed_union, isSigned = false,
            isFourState = false, name = "choice_t", range = [], children = [
              #obelisk_sim.vpi_type<kind = bit, isSigned = false,
                isFourState = false, range = [0, 0], children = [],
                childNames = []>], childNames = ["member"], isTagged = false,
            isSoft = false, bitWidth = 1 : i64,
            selectableWidth = 1 : i64, bitstreamWidth = 1 : i64,
            tagBits = 0 : i64, childOrdinals = [0],
            childPackedOffsets = [0]>], childNames = []>}

    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}

// Exact source scalar flavors survive normalized executable types and become
// the concrete vpiType of each immutable design handle.
// CHECK: object name=top.bit_net kind=3 vpi_kind=532 {{.*}} width=1
// CHECK-NEXT: object name=top.bit_value kind=2 vpi_kind=620 {{.*}} width=1
// CHECK-NEXT: object name=top.initial kind=5 vpi_kind=24
// CHECK-NEXT: object name=top.int_value kind=2 vpi_kind=612 {{.*}} width=32
// CHECK-NEXT: object name=top.logic_net kind=3 vpi_kind=36 {{.*}} width=1
// CHECK-NEXT: object name=top.logic_vector kind=2 vpi_kind=48 {{.*}} width=8
// CHECK-NEXT: object name=top.packed_record_net kind=3 vpi_kind=693 {{.*}} width=2
// CHECK-NEXT: object name=top.packed_record_value kind=2 vpi_kind=623 {{.*}} width=2
// CHECK-NEXT: object name=top.packed_union_net kind=3 vpi_kind=525 {{.*}} width=2
// CHECK-NEXT: object name=top.real_value kind=2 vpi_kind=47 {{.*}} width=64
// CHECK-NEXT: object name=top.shortreal_value kind=2 vpi_kind=613 {{.*}} width=32 {{.*}} type_kind=1 type_flags=0x0
