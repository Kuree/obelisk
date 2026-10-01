// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' --emit-bytecode -o /dev/null

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @s2.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 2 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s3.top attributes {hierarchical_name = "top", name = "top", node_id = 3 : i64} {
        obelisk.sv.symbol.parameter @s4.inf attributes {constant_value = "inf", hierarchical_name = "top.inf", name = "inf", node_id = 4 : i64, semantic_type = !obelisk.real} {
        }
        obelisk.sv.symbol.parameter @s5.nan attributes {constant_value = "-nan", hierarchical_name = "top.nan", name = "nan", node_id = 5 : i64, semantic_type = !obelisk.real} {
        }
        obelisk.sv.symbol.variable @s6.value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 6 : i64, semantic_type = !obelisk.real} {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 9 : i64, semantic_type = !obelisk.real} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s2.top::@s3.top::@s6.value, semantic_type = !obelisk.real} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.inf", referenced_symbol = @s1.$root::@s2.top::@s3.top::@s4.inf, semantic_type = !obelisk.real} {
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 13 : i64, semantic_type = !obelisk.real} {
              obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s2.top::@s3.top::@s6.value, semantic_type = !obelisk.real} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 15 : i64, referenced_path = "top.nan", referenced_symbol = @s1.$root::@s2.top::@s3.top::@s5.nan, semantic_type = !obelisk.real} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: arith.constant 0x7FF0000000000000 : f64
// CHECK: arith.constant 0xFFF8000000000000 : f64
