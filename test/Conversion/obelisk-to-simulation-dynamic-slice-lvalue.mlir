// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "dynamic_slice_lvalue", name = "dynamic_slice_lvalue", node_id = 0 : i64, sym_name = "s0.dynamic_slice_lvalue"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "dynamic_slice_lvalue", is_uninstantiated = false, name = "dynamic_slice_lvalue", node_id = 3 : i64, referenced_path = "dynamic_slice_lvalue", referenced_symbol = @s0.dynamic_slice_lvalue, sym_name = "s3.dynamic_slice_lvalue"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "dynamic_slice_lvalue", name = "dynamic_slice_lvalue", node_id = 4 : i64, sym_name = "s4.dynamic_slice_lvalue"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_slice_lvalue.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_slice_lvalue.index", lifetime = 1 : i32, name = "index", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<6 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.index"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_slice_lvalue.replacement", lifetime = 1 : i32, name = "replacement", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s7.replacement"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "dynamic_slice_lvalue", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<11 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 11 : i64, selection_kind = 2 : i32, semantic_type = !obelisk.ranged_packed_array<11 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "dynamic_slice_lvalue.value", referenced_symbol = @s1.$root::@s3.dynamic_slice_lvalue::@s4.dynamic_slice_lvalue::@s5.value, semantic_type = !obelisk.ranged_packed_array<11 : 4 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "dynamic_slice_lvalue.index", referenced_symbol = @s1.$root::@s3.dynamic_slice_lvalue::@s4.dynamic_slice_lvalue::@s6.index, semantic_type = !obelisk.ranged_packed_array<6 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "4", is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "dynamic_slice_lvalue.replacement", referenced_symbol = @s1.$root::@s3.dynamic_slice_lvalue::@s4.dynamic_slice_lvalue::@s7.replacement, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.logic.compare case_eq
// CHECK: arith.cmpi sge
// CHECK: arith.cmpi sle
// CHECK: obelisk_sim.ref.dyn_extract

// IEEE 1800-2017 11.5.1: only the bits in range are written. A slice hanging
// off the low end loses the replacement's low bits, so the surviving bits come
// from further up the replacement the further the slice hangs off.
// CHECK: obelisk_sim.logic.extract {{%.*}} from 1 : !obelisk_sim.logic<4> -> !obelisk_sim.logic<3>
// CHECK: obelisk_sim.ref.extract {{%.*}} from 0 : {{.*}} -> !obelisk_sim.ref<!obelisk_sim.logic<3>>
// CHECK: obelisk_sim.ref.store {{%.*}} to {{%.*}} : !obelisk_sim.logic<3>, !obelisk_sim.ref<!obelisk_sim.logic<3>>

// A slice hanging off the high end instead keeps the replacement's low bits.
// CHECK: obelisk_sim.logic.extract {{%.*}} from 0 : !obelisk_sim.logic<4> -> !obelisk_sim.logic<1>
// CHECK: obelisk_sim.ref.extract {{%.*}} from 7 : {{.*}} -> !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-NOT: obelisk.sv.
