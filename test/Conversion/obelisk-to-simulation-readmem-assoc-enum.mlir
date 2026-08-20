// RUN: obelisk-opt %s --lower-obelisk-to-sim | FileCheck %s

// Enum-indexed associative memories use the numeric enum values as their
// legal address set. File addresses are narrowed and sign-extended first.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s3.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "s4.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "top.NEG", name = "NEG", node_id = 5 : i64, sym_name = "s5.NEG"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "top.MID", name = "MID", node_id = 6 : i64, sym_name = "s6.MID"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "top.HIGH", name = "HIGH", node_id = 7 : i64, sym_name = "s7.HIGH"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "top.key_t", name = "key_t", node_id = 8 : i64, semantic_type = !obelisk.enum<"top.key_t", !obelisk.integral<32, true, false, 31 : 0, int>>, sym_name = "s8.key_t"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "top.A", name = "A", node_id = 9 : i64, sym_name = "s9.A"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "top.B", name = "B", node_id = 10 : i64, sym_name = "s10.B"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "top.C", name = "C", node_id = 11 : i64, sym_name = "s11.C"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "top.value_t", name = "value_t", node_id = 12 : i64, semantic_type = !obelisk.enum<"top.value_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s12.value_t"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.memory", lifetime = 1 : i32, name = "memory", node_id = 13 : i64, semantic_type = !obelisk.assoc<!obelisk.enum<"top.key_t", !obelisk.integral<32, true, false, 31 : 0, int>>, !obelisk.enum<"top.value_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, false>, sym_name = "s13.memory"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 14 : i64, procedure_kind = 0 : i32, sym_name = "s14", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$readmemh", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 16 : i64, readmem_enum_element_values = ["4'b1", "4'b100", "4'b1001"], readmem_enum_key_values = ["-2", "3", "9"], semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              obelisk.sv.expression.string_literal attributes {constant_value = "assoc.mem", is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.ranged_packed_array<71 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.assoc<!obelisk.enum<"top.key_t", !obelisk.integral<32, true, false, 31 : 0, int>>, !obelisk.enum<"top.value_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, false>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "top.memory", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s13.memory, semantic_type = !obelisk.assoc<!obelisk.enum<"top.key_t", !obelisk.integral<32, true, false, 31 : 0, int>>, !obelisk.enum<"top.value_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, false>} {
                }
                obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.assoc<!obelisk.enum<"top.key_t", !obelisk.integral<32, true, false, 31 : 0, int>>, !obelisk.enum<"top.value_t", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, false>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.assoc.create {{.*}}key_kind = 2 : i32, key_width = 32
// CHECK: %[[DATA:.*]], %[[KIND:.*]], %[[ADDRESS:.*]] = obelisk_sim.file.readmem_token
// CHECK: %[[NARROW:.*]] = arith.trunci %[[ADDRESS]] : i64 to i32
// CHECK: %[[NORMALIZED:.*]] = arith.extsi %[[NARROW]] : i32 to i64
// CHECK: arith.cmpi eq, %[[NORMALIZED]], %{{.*-2.*}} : i64
// CHECK: arith.cmpi eq, %[[NORMALIZED]], %{{.*3.*}} : i64
// CHECK: arith.cmpi eq, %[[NORMALIZED]], %{{.*9.*}} : i64
// CHECK: obelisk_sim.logic.compare case_eq %[[DATA]],
// CHECK: cf.cond_br {{.*}}, ^[[WRITE:bb[0-9]+]], ^[[ENUM_ERROR:bb[0-9]+]]
// CHECK: ^[[WRITE]]:
// CHECK: obelisk_sim.assoc.write
// CHECK: ^[[ENUM_ERROR]]:
// CHECK: obelisk_sim.display
