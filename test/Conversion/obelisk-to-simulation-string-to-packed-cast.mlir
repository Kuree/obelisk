// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.24.3 treats a string as a dynamically sized bit-stream
// source. A cast to a fixed packed target must therefore check the runtime
// string width and issue an error on a mismatch. Clause 6.16's truncation and
// padding rule applies to string literals, not string variables.
module {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s attributes {hierarchical_name = "top.s", lifetime = 1 : i32, name = "s", node_id = 5 : i64, semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.variable @d attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "top.d", referenced_symbol = @root::@instance::@body::@d, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.conversion attributes {is_implicit = false, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.s", referenced_symbol = @root::@instance::@body::@s, semantic_type = !obelisk.string} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: %{{.*}}, %{{.*}} = simulation.string.to_packed_exact %{{.*}} : (!simulation.string) -> (i32, i1)
// CHECK: cf.cond_br
// CHECK: bit-stream cast source and destination widths differ
// CHECK-NOT: simulation.string.to_packed %
