// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "string_parameter", name = "string_parameter", node_id = 0 : i64, sym_name = "s0.string_parameter"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "string_parameter", is_uninstantiated = false, name = "string_parameter", node_id = 3 : i64, referenced_path = "string_parameter", referenced_symbol = @s0.string_parameter, sym_name = "s3.string_parameter"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "string_parameter", name = "string_parameter", node_id = 4 : i64, sym_name = "s4.string_parameter"} {
        obelisk.sv.symbol.parameter attributes {constant_value = "the_key", hierarchical_name = "string_parameter.key", name = "key", node_id = 5 : i64, semantic_type = !obelisk.string, sym_name = "s5.key"} {
          obelisk.sv.expression.conversion attributes {node_id = 6 : i64, semantic_type = !obelisk.string} {
            obelisk.sv.expression.string_literal attributes {constant_value = "the_key", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<55 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "string_parameter.result", lifetime = 1 : i32, name = "result", node_id = 8 : i64, semantic_type = !obelisk.string, sym_name = "s8.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "string_parameter", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 11 : i64, semantic_type = !obelisk.string} {
              obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "string_parameter.result", referenced_symbol = @s1.$root::@s3.string_parameter::@s4.string_parameter::@s8.result, semantic_type = !obelisk.string} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "string_parameter.key", referenced_symbol = @s1.$root::@s3.string_parameter::@s4.string_parameter::@s5.key, semantic_type = !obelisk.string} {
              }
            }
          }
        }
      }
    }
  }
}

// String parameter constant_value carries decoded bytes, without source-level
// quote delimiters.
// CHECK: %[[KEY:.*]] = obelisk_sim.string.literal "the_key"
// CHECK-NOT: obelisk_sim.string.literal "\22the_key\22"
// CHECK: obelisk_sim.ref.store %[[KEY]]
// CHECK-NOT: obelisk.sv.
