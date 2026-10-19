// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --implicit-check-not='!simulation.logic'

// Declared source kinds and state domains survive physical two-state lowering.
// Verify scalar, enum and aggregate recursion without passing through Slang.
!logic = !obelisk.integral<1, false, false, 0 : 0, logic>
!reg = !obelisk.integral<1, false, false, 0 : 0, reg>
!integer = !obelisk.integral<32, true, false, 31 : 0, integer>
!time = !obelisk.integral<64, false, false, 63 : 0, time>
!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!enum = !obelisk.enum<"state_t", !obelisk.ranged_packed_array<1 : 0 x !logic>>
!record = !obelisk.source_aggregate<"record_t", true, false, false, false,
  false, false, 2, 2, 2, 0,
  [{name = "data", ordinal = 0 : i32, packed_offset = 1 : i64, type = !logic},
   {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !bit}]>
module attributes {native.two_state} {
  obelisk.sv.symbol.definition @def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 2 : i64, referenced_path = "top", referenced_symbol = @def} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 3 : i64} {
        obelisk.sv.symbol.enum_value @IDLE attributes {constant_value = "2'b00", hierarchical_name = "top.state_t.IDLE", name = "IDLE", node_id = 10 : i64, semantic_type = !enum, vpi_source_type_identity = 1 : i64} {}
        obelisk.sv.symbol.variable @a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 4 : i64, semantic_type = !logic} {}
        obelisk.sv.symbol.variable @b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 5 : i64, semantic_type = !reg} {}
        obelisk.sv.symbol.variable @c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 6 : i64, semantic_type = !integer} {}
        obelisk.sv.symbol.variable @d attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 7 : i64, semantic_type = !time} {}
        obelisk.sv.symbol.variable @e attributes {hierarchical_name = "top.e", lifetime = 1 : i32, name = "e", node_id = 8 : i64, semantic_type = !enum, vpi_source_type_identity = 1 : i64} {}
        obelisk.sv.symbol.variable @f attributes {hierarchical_name = "top.f", lifetime = 1 : i32, name = "f", node_id = 9 : i64, semantic_type = !record} {}
      }
    }
  }
}

// CHECK-DAG: : i1 design hierarchy "top.a" {{.*}}kind = logic, isSigned = false, isFourState = true{{.*}}projected = true
// CHECK-DAG: : i1 design hierarchy "top.b" {{.*}}kind = reg, isSigned = false, isFourState = true{{.*}}projected = true
// CHECK-DAG: : i32 design hierarchy "top.c" {{.*}}kind = integer, isSigned = true, isFourState = true{{.*}}projected = true
// CHECK-DAG: : i64 design hierarchy "top.d" {{.*}}kind = time, isSigned = false, isFourState = true{{.*}}projected = true
// CHECK-DAG: : i2 design hierarchy "top.e" {{.*}}kind = enum, isSigned = false, isFourState = true{{.*}}projected = true
// CHECK-DAG: hierarchy "top.f" {{.*}}kind = packed_struct, isSigned = false, isFourState = true{{.*}}kind = logic, isSigned = false, isFourState = true{{.*}}projected = true{{.*}}kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>{{.*}}projected = true
