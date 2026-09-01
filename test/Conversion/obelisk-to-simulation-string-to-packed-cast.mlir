// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 6.24.3 treats a string as a dynamically sized bit-stream
// source. A cast to a fixed packed target must therefore check the runtime
// string width and issue an error on a mismatch. Clause 6.16's truncation and
// padding rule applies to string literals, not string variables.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "unit"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "instance"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.s", lifetime = 1 : i32, name = "s", node_id = 5 : i64, semantic_type = !obelisk.string, sym_name = "s"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", lifetime = 1 : i32, name = "d", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "d"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "initial", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
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

// CHECK: %{{.*}}, %{{.*}} = obelisk_sim.string.to_packed_exact %{{.*}} : (!obelisk_sim.string) -> (i32, i1)
// CHECK: cf.cond_br
// CHECK: bit-stream cast source and destination widths differ
// CHECK-NOT: obelisk_sim.string.to_packed %
