// RUN: obelisk-opt %s --lower-obelisk-to-sim | FileCheck %s
// Lowered simulation IR has to re-parse, and a packed-array key is not a
// normalized integral key, so the round trip is part of the contract.
// RUN: obelisk-opt %s --lower-obelisk-to-sim | obelisk-opt | FileCheck %s

// MLIR-level coverage for vector-typed associative-array keys. An integral key
// indexes on its value rather than on its declared packed shape, so a
// `ranged_packed_array` key normalizes to the scalar of the same width and
// state domain instead of staying an aggregate. Two-state vectors collapse to a
// signless integer, four-state vectors keep their state domain as a logic
// value, and non-packed keys such as strings have no scalar and pass through.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        // A 64-bit two-state vector key collapses to a signless i64.
        // CHECK-DAG: assoc_array<i64, i32, false, false>
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.wide", lifetime = 1 : i32, name = "wide", node_id = 5 : i64, semantic_type = !obelisk.assoc<!obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, !obelisk.integral<32, true, false, 31 : 0, int>, false>, sym_name = "s5.wide"} {
        }
        // Narrow vectors collapse the same way; the width is preserved exactly.
        // CHECK-DAG: assoc_array<i8, i32, false, false>
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.narrow", lifetime = 1 : i32, name = "narrow", node_id = 6 : i64, semantic_type = !obelisk.assoc<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, !obelisk.integral<32, true, false, 31 : 0, int>, false>, sym_name = "s6.narrow"} {
        }
        // A four-state vector key stays four-state rather than becoming i64.
        // CHECK-DAG: assoc_array<!obelisk_sim.logic<64>, i32, false, false>
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.four", lifetime = 1 : i32, name = "four", node_id = 7 : i64, semantic_type = !obelisk.assoc<!obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, !obelisk.integral<32, true, false, 31 : 0, int>, false>, sym_name = "s7.four"} {
        }
        // A string key has no packed scalar and is left alone.
        // CHECK-DAG: assoc_array<!obelisk_sim.string, i32, false, false>
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.named", lifetime = 1 : i32, name = "named", node_id = 8 : i64, semantic_type = !obelisk.assoc<!obelisk.string, !obelisk.integral<32, true, false, 31 : 0, int>, false>, sym_name = "s8.named"} {
        }
        // A signed integral key keeps its signed-key marker.
        // CHECK-DAG: assoc_array<i32, i32, true, false>
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.signed_key", lifetime = 1 : i32, name = "signed_key", node_id = 9 : i64, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, false>, sym_name = "s9.signed_key"} {
        }
      }
    }
  }
}

// CHECK-NOT: assoc_array<!obelisk_sim.packed_array
