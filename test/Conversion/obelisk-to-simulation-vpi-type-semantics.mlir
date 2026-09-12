// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!logic_up = !obelisk.integral<8, false, true, -2 : 5, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!integer = !obelisk.integral<32, true, true, 31 : 0, integer>
!state = !obelisk.enum<"state_t", !obelisk.integral<2, false, true, 1 : 0, logic>>
!cu_state = !obelisk.enum<"cu_state_t", !obelisk.integral<1, false, false, 0 : 0, bit>>
!packed = !obelisk.ranged_packed_array<0 : 3 x !bit>
!record = !obelisk.source_aggregate<"record_t", false, false, false, false,
    false, false, 0, 40, 40, 0,
    [{name = "count", ordinal = 0 : i32, packed_offset = 0 : i64,
      type = !int},
     {name = "flags", ordinal = 1 : i32, packed_offset = 0 : i64,
      type = !obelisk.ranged_packed_array<7 : 0 x !bit>}]>
!tagged = !obelisk.source_aggregate<"choice_t", false, true, true, false,
    false, false, 0, 4, 4, 0,
    [{name = "none", ordinal = 0 : i32, packed_offset = 0 : i64,
      type = !obelisk.void},
     {name = "value", ordinal = 1 : i32, packed_offset = 0 : i64,
      type = !obelisk.ranged_packed_array<3 : 0 x !bit>}]>
!wild = !obelisk.assoc<!obelisk.untyped, !obelisk.string, true>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.symbol.enum_value attributes {constant_value = "1'b0", hierarchical_name = "$unit::CU_IDLE", name = "CU_IDLE", node_id = 34 : i64, semantic_type = !cu_state, sym_name = "s34.CU_IDLE", vpi_source_type_identity = 8 : i64} {
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.type.type_alias attributes {hierarchical_name = "top.flags_t", name = "flags_t", node_id = 23 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !bit>, sym_name = "s23.flags_t", vpi_typedef_layers = [{aliases = [@s1.$root::@s3.top::@s4.top::@s23.flags_t], path = array<i64>}]} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "top.record_t", name = "record_t", node_id = 24 : i64, semantic_type = !record, sym_name = "s24.record_t", vpi_typedef_layers = [{aliases = [@s1.$root::@s3.top::@s4.top::@s24.record_t], path = array<i64>}, {aliases = [@s1.$root::@s3.top::@s4.top::@s23.flags_t], path = array<i64: 1>}]} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "top.outer_t", name = "outer_t", node_id = 25 : i64, semantic_type = !record, sym_name = "s25.outer_t", vpi_typedef_layers = [{aliases = [@s1.$root::@s3.top::@s4.top::@s25.outer_t, @s1.$root::@s3.top::@s4.top::@s24.record_t], path = array<i64>}, {aliases = [@s1.$root::@s3.top::@s4.top::@s23.flags_t], path = array<i64: 1>}]} {
        }
        // These declarations are deliberately unused by any value object.
        // vpiTypedef still requires all three in the scope inventory.
        obelisk.sv.type.type_alias attributes {hierarchical_name = "top.unused_t", name = "unused_t", node_id = 29 : i64, semantic_type = !bit, sym_name = "s29.unused_t", vpi_typedef_layers = [{aliases = [@s1.$root::@s3.top::@s4.top::@s29.unused_t], path = array<i64>}]} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "top.sequence_t", name = "sequence_t", node_id = 30 : i64, semantic_type = !obelisk.sequence, sym_name = "s30.sequence_t", vpi_typedef_layers = [{aliases = [@s1.$root::@s3.top::@s4.top::@s30.sequence_t], path = array<i64>}]} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "top.property_t", name = "property_t", node_id = 31 : i64, semantic_type = !obelisk.property, sym_name = "s31.property_t", vpi_typedef_layers = [{aliases = [@s1.$root::@s3.top::@s4.top::@s31.property_t], path = array<i64>}]} {
        }
        obelisk.sv.symbol.enum_value attributes {constant_value = "2'b00", hierarchical_name = "top.state_t.IDLE", name = "IDLE", node_id = 32 : i64, semantic_type = !state, sym_name = "s32.IDLE", vpi_source_type_identity = 7 : i64} {
        }
        obelisk.sv.symbol.enum_value attributes {constant_value = "2'b1x", hierarchical_name = "top.state_t.ACTIVE", name = "ACTIVE", node_id = 33 : i64, semantic_type = !state, sym_name = "s33.ACTIVE", vpi_source_type_identity = 7 : i64} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bit_value", lifetime = 1 : i32, name = "bit_value", node_id = 5 : i64, semantic_type = !bit, sym_name = "s5.bit_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.logic_up", lifetime = 1 : i32, name = "logic_up", node_id = 6 : i64, semantic_type = !logic_up, sym_name = "s6.logic_up"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.int_value", lifetime = 1 : i32, name = "int_value", node_id = 7 : i64, semantic_type = !int, sym_name = "s7.int_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.integer_value", lifetime = 1 : i32, name = "integer_value", node_id = 8 : i64, semantic_type = !integer, sym_name = "s8.integer_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.state", lifetime = 1 : i32, name = "state", node_id = 9 : i64, semantic_type = !state, sym_name = "s9.state", vpi_source_type_identity = 7 : i64} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.packed", lifetime = 1 : i32, name = "packed", node_id = 10 : i64, semantic_type = !packed, sym_name = "s10.packed"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.record", lifetime = 1 : i32, name = "record", node_id = 11 : i64, semantic_type = !record, sym_name = "s11.record"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.time_value", lifetime = 1 : i32, name = "time_value", node_id = 12 : i64, semantic_type = !obelisk.time, sym_name = "s12.time_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.shortreal_value", lifetime = 1 : i32, name = "shortreal_value", node_id = 13 : i64, semantic_type = !obelisk.shortreal, sym_name = "s13.shortreal_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.real_value", lifetime = 1 : i32, name = "real_value", node_id = 14 : i64, semantic_type = !obelisk.real, sym_name = "s14.real_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.realtime_value", lifetime = 1 : i32, name = "realtime_value", node_id = 15 : i64, semantic_type = !obelisk.realtime, sym_name = "s15.realtime_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.string_value", lifetime = 1 : i32, name = "string_value", node_id = 16 : i64, semantic_type = !obelisk.string, sym_name = "s16.string_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.chandle_value", lifetime = 1 : i32, name = "chandle_value", node_id = 17 : i64, semantic_type = !obelisk.chandle, sym_name = "s17.chandle_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.dynamic_value", lifetime = 1 : i32, name = "dynamic_value", node_id = 18 : i64, semantic_type = !obelisk.dynarray<!int>, sym_name = "s18.dynamic_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.queue_value", lifetime = 1 : i32, name = "queue_value", node_id = 19 : i64, semantic_type = !obelisk.queue<!bit, 7>, sym_name = "s19.queue_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.assoc_value", lifetime = 1 : i32, name = "assoc_value", node_id = 20 : i64, semantic_type = !obelisk.assoc<!int, !obelisk.string, false>, sym_name = "s20.assoc_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.process_value", lifetime = 1 : i32, name = "process_value", node_id = 21 : i64, semantic_type = !obelisk.process, sym_name = "s21.process_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.aliased_record", lifetime = 1 : i32, name = "aliased_record", node_id = 26 : i64, semantic_type = !record, sym_name = "s26.aliased_record", vpi_typedef_layers = [{aliases = [@s1.$root::@s3.top::@s4.top::@s25.outer_t, @s1.$root::@s3.top::@s4.top::@s24.record_t], path = array<i64>}, {aliases = [@s1.$root::@s3.top::@s4.top::@s23.flags_t], path = array<i64: 1>}]} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.tagged", lifetime = 1 : i32, name = "tagged", node_id = 27 : i64, semantic_type = !tagged, sym_name = "s27.tagged"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.wild", lifetime = 1 : i32, name = "wild", node_id = 28 : i64, semantic_type = !wild, sym_name = "s28.wild"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "top.net_value", is_implicit = false, name = "net_value", net_kind = 1 : i32, node_id = 22 : i64, semantic_type = !logic_up, sym_name = "s22.net_value"} {
        }
      }
    }
  }
}

// The executable types are normalized, while the immutable declaration
// inventory retains the exact source semantic needed by VPI.
// CHECK-DAG: hierarchy "top.bit_value" {{.*}}vpi_type = #obelisk_sim.vpi_type<kind = bit, isSigned = false, isFourState = false, range = [0, 0], children = [], childNames = []>
// CHECK-DAG: hierarchy "top.logic_up" {{.*}}vpi_type = #obelisk_sim.vpi_type<kind = logic, isSigned = false, isFourState = true, range = [-2, 5], children = [], childNames = []>
// CHECK-DAG: hierarchy "top.int_value" {{.*}}vpi_type = #obelisk_sim.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
// CHECK-DAG: hierarchy "top.integer_value" {{.*}}vpi_type = #obelisk_sim.vpi_type<kind = integer, isSigned = true, isFourState = true, range = [31, 0], children = [], childNames = []>
// CHECK-DAG: hierarchy "top.state" {{.*}}obelisk_sim.vpi_source_type_identity = 7 : i64{{.*}}vpi_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false, isFourState = true, name = "state_t"{{.*}}kind = logic{{.*}}range = [1, 0]
// CHECK-DAG: hierarchy "top.packed" {{.*}}vpi_type = #obelisk_sim.vpi_type<kind = packed_array{{.*}}range = [0, 3]{{.*}}kind = bit
// CHECK-DAG: hierarchy "top.record" {{.*}}vpi_type = #obelisk_sim.vpi_type<kind = unpacked_struct{{.*}}name = "record_t"{{.*}}childNames = ["count", "flags"], isTagged = false, isSoft = false, bitWidth = 0 : i64, selectableWidth = 40 : i64, bitstreamWidth = 40 : i64, tagBits = 0 : i64, childOrdinals = [0, 1], childPackedOffsets = [0, 0], childRandTypes = [1, 1]>
// CHECK-DAG: hierarchy "top.time_value" {{.*}}kind = time{{.*}}isFourState = true{{.*}}range = [63, 0]
// CHECK-DAG: hierarchy "top.shortreal_value" {{.*}}kind = shortreal
// CHECK-DAG: hierarchy "top.real_value" {{.*}}kind = real
// CHECK-DAG: hierarchy "top.realtime_value" {{.*}}kind = realtime
// CHECK-DAG: hierarchy "top.string_value" {{.*}}kind = string
// CHECK-DAG: hierarchy "top.chandle_value" {{.*}}kind = chandle
// CHECK-DAG: hierarchy "top.dynamic_value" {{.*}}kind = dynamic_array{{.*}}kind = int
// CHECK-DAG: hierarchy "top.queue_value" {{.*}}kind = queue{{.*}}kind = bit{{.*}}queueBound = 7 : i64
// CHECK-DAG: hierarchy "top.assoc_value" {{.*}}kind = assoc_array{{.*}}kind = int{{.*}}kind = string{{.*}}wildcardIndex = false
// CHECK-DAG: hierarchy "top.process_value" {{.*}}kind = process
// CHECK-DAG: hierarchy "top.tagged" {{.*}}kind = unpacked_union{{.*}}name = "choice_t"{{.*}}kind = void{{.*}}childNames = ["none", "value"], isTagged = true, isSoft = false, bitWidth = 0 : i64, selectableWidth = 4 : i64, bitstreamWidth = 4 : i64, tagBits = 0 : i64{{.*}}childOrdinals = [0, 1], childPackedOffsets = [0, 0], childRandTypes = [1, 1]
// CHECK-DAG: hierarchy "top.wild" {{.*}}kind = assoc_array{{.*}}kind = untyped{{.*}}kind = string{{.*}}wildcardIndex = true
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[CU_ANCHOR:__obelisk_vpi_anchor_0]] id 0 type 600 in 0 {{.*}}hierarchy "$unit" debug ""
// CHECK-DAG: obelisk_sim.vpi_object.anchor @[[TOP_ANCHOR:__obelisk_vpi_anchor_1]] id 1 type 32 in 1 {{.*}}hierarchy "top" debug "top"
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @__obelisk_vpi_typespec_0 id 0 in 1 owner @[[TOP_ANCHOR]] hierarchy "top.flags_t" debug "flags_t" {{.*}}typedefAliases = [@__obelisk_vpi_typespec_0]
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @__obelisk_vpi_typespec_1 id 1 in 1 owner @[[TOP_ANCHOR]] hierarchy "top.record_t" debug "record_t" {{.*}}typedefAliases = [@__obelisk_vpi_typespec_0]{{.*}}typedefAliases = [@__obelisk_vpi_typespec_1]
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @__obelisk_vpi_typespec_2 id 2 in 1 owner @[[TOP_ANCHOR]] hierarchy "top.outer_t" debug "outer_t" {{.*}}typedefAliases = [@__obelisk_vpi_typespec_2, @__obelisk_vpi_typespec_1]
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @__obelisk_vpi_typespec_3 id 3 in 1 owner @[[TOP_ANCHOR]] hierarchy "top.unused_t" debug "unused_t" {{.*}}kind = bit{{.*}}typedefAliases = [@__obelisk_vpi_typespec_3]
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @__obelisk_vpi_typespec_4 id 4 in 1 owner @[[TOP_ANCHOR]] hierarchy "top.sequence_t" debug "sequence_t" {{.*}}kind = sequence{{.*}}typedefAliases = [@__obelisk_vpi_typespec_4]
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @__obelisk_vpi_typespec_5 id 5 in 1 owner @[[TOP_ANCHOR]] hierarchy "top.property_t" debug "property_t" {{.*}}kind = property{{.*}}typedefAliases = [@__obelisk_vpi_typespec_5]
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @[[CU_TS:__obelisk_vpi_enum_typespec_6]] id 6 in 0 owner @[[CU_ANCHOR]] hierarchy "$unit" debug "cu_state_t" {{.*}}origin = 2 : i32{{.*}}kind = enum
// CHECK-DAG: obelisk_sim.vpi_typespec.decl @[[STATE_TS:__obelisk_vpi_enum_typespec_7]] id 7 in 1 owner @[[TOP_ANCHOR]] hierarchy "top.state_t" debug "state_t" {{.*}}origin = 2 : i32{{.*}}kind = enum
// CHECK-DAG: obelisk_sim.vpi_enum_const.decl 0 enum @[[CU_TS]] ordinal 0 name "CU_IDLE" value "1'b0"
// CHECK-DAG: obelisk_sim.vpi_enum_const.decl 1 enum @[[STATE_TS]] ordinal 0 name "IDLE" value "2'b00"
// CHECK-DAG: obelisk_sim.vpi_enum_const.decl 2 enum @[[STATE_TS]] ordinal 1 name "ACTIVE" value "2'b1x"
// CHECK-DAG: hierarchy "top.aliased_record" {{.*}}typedefAliases = [@__obelisk_vpi_typespec_0]{{.*}}typedefAliases = [@__obelisk_vpi_typespec_2, @__obelisk_vpi_typespec_1]
// CHECK-DAG: obelisk_sim.net.decl {{.*}}hierarchy "top.net_value" {{.*}}kind = logic{{.*}}range = [-2, 5]
// CHECK-NOT: obelisk.sv.
