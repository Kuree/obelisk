// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "sdf_applied", name = "sdf_applied", node_id = 0 : i64, sym_name = "s0.sdf_applied"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "sdf_applied", is_uninstantiated = false, name = "sdf_applied", node_id = 3 : i64, referenced_path = "sdf_applied", referenced_symbol = @s0.sdf_applied, sym_name = "s3.sdf_applied"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "sdf_applied", name = "sdf_applied", node_id = 4 : i64, sym_name = "s4.sdf_applied"} {
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sdf_applied", node_id = 5 : i64, procedure_kind = 0 : i32, sym_name = "s5", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 6 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$sdf_annotate", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 7 : i64, obelisk.sdf_compile_time_applied, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.sdf_applied", system_scope_path = "sdf_applied", system_scope_symbol = @s1.$root::@s3.sdf_applied::@s4.sdf_applied} {
            }
          }
        }
      }
    }
  }
}

// CHECK: module {
// CHECK-NOT: sdf_annotate
// CHECK-NOT: obelisk.sdf
// CHECK-NOT: sdf.table
// CHECK-NOT: sdf.reader
