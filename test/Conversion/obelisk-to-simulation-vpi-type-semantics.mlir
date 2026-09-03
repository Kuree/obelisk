// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!logic_up = !obelisk.integral<8, false, true, -2 : 5, logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!integer = !obelisk.integral<32, true, true, 31 : 0, integer>
!state = !obelisk.enum<"state_t", !obelisk.integral<2, false, true, 1 : 0, logic>>
!packed = !obelisk.ranged_packed_array<0 : 3 x !bit>
!record = !obelisk.source_aggregate<"record_t", false, false, false, false,
    false, false, 0, 40, 40, 0,
    [{name = "count", ordinal = 0 : i32, packed_offset = 0 : i64,
      type = !int},
     {name = "flags", ordinal = 1 : i32, packed_offset = 0 : i64,
      type = !obelisk.ranged_packed_array<7 : 0 x !bit>}]>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.bit_value", lifetime = 1 : i32, name = "bit_value", node_id = 5 : i64, semantic_type = !bit, sym_name = "s5.bit_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.logic_up", lifetime = 1 : i32, name = "logic_up", node_id = 6 : i64, semantic_type = !logic_up, sym_name = "s6.logic_up"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.int_value", lifetime = 1 : i32, name = "int_value", node_id = 7 : i64, semantic_type = !int, sym_name = "s7.int_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.integer_value", lifetime = 1 : i32, name = "integer_value", node_id = 8 : i64, semantic_type = !integer, sym_name = "s8.integer_value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.state", lifetime = 1 : i32, name = "state", node_id = 9 : i64, semantic_type = !state, sym_name = "s9.state"} {
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
// CHECK-DAG: hierarchy "top.state" {{.*}}vpi_type = #obelisk_sim.vpi_type<kind = enum, isSigned = false, isFourState = true, name = "state_t"{{.*}}kind = logic{{.*}}range = [1, 0]
// CHECK-DAG: hierarchy "top.packed" {{.*}}vpi_type = #obelisk_sim.vpi_type<kind = packed_array{{.*}}range = [0, 3]{{.*}}kind = bit
// CHECK-DAG: hierarchy "top.record" {{.*}}vpi_type = #obelisk_sim.vpi_type<kind = unpacked_struct{{.*}}name = "record_t"{{.*}}childNames = ["count", "flags"]
// CHECK-DAG: hierarchy "top.time_value" {{.*}}kind = time{{.*}}isFourState = true{{.*}}range = [63, 0]
// CHECK-DAG: hierarchy "top.shortreal_value" {{.*}}kind = shortreal
// CHECK-DAG: hierarchy "top.real_value" {{.*}}kind = real
// CHECK-DAG: hierarchy "top.realtime_value" {{.*}}kind = realtime
// CHECK-DAG: hierarchy "top.string_value" {{.*}}kind = string
// CHECK-DAG: hierarchy "top.chandle_value" {{.*}}kind = chandle
// CHECK-DAG: hierarchy "top.dynamic_value" {{.*}}kind = dynamic_array{{.*}}kind = int
// CHECK-DAG: hierarchy "top.queue_value" {{.*}}kind = queue{{.*}}kind = bit
// CHECK-DAG: hierarchy "top.assoc_value" {{.*}}kind = assoc_array{{.*}}kind = int{{.*}}kind = string
// CHECK-DAG: hierarchy "top.process_value" {{.*}}kind = process
// CHECK-DAG: obelisk_sim.net.decl {{.*}}hierarchy "top.net_value" {{.*}}kind = logic{{.*}}range = [-2, 5]
// CHECK-NOT: obelisk.sv.
