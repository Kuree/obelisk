// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 20.5: $itor takes an integer argument, so a real argument is
// first converted to an integer -- rounded, with a non-finite value becoming
// zero -- before it is converted back to a real.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "itor_real_argument", name = "itor_real_argument", node_id = 0 : i64, sym_name = "s0.itor_real_argument"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "itor_real_argument", is_uninstantiated = false, name = "itor_real_argument", node_id = 3 : i64, referenced_path = "itor_real_argument", referenced_symbol = @s0.itor_real_argument, sym_name = "s3.itor_real_argument"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "itor_real_argument", name = "itor_real_argument", node_id = 4 : i64, sym_name = "s4.itor_real_argument", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "itor_real_argument.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.real, sym_name = "s5.source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "itor_real_argument.result", lifetime = 1 : i32, name = "result", node_id = 6 : i64, semantic_type = !obelisk.real, sym_name = "s6.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "itor_real_argument", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.real} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "itor_real_argument.result", referenced_symbol = @s1.$root::@s3.itor_real_argument::@s4.itor_real_argument::@s6.result, semantic_type = !obelisk.real} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$itor", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.itor_real_argument", system_scope_path = "itor_real_argument", system_scope_symbol = @s1.$root::@s3.itor_real_argument::@s4.itor_real_argument} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "itor_real_argument.source", referenced_symbol = @s1.$root::@s3.itor_real_argument::@s4.itor_real_argument::@s5.source, semantic_type = !obelisk.real} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: %[[SOURCE:.*]] = obelisk_sim.ref.load
// CHECK: %[[INTEGER:.*]] = obelisk_sim.real.to_integer %[[SOURCE]] signed = true : i32
// CHECK: obelisk_sim.real.from_integer %[[INTEGER]] signed = true : i32 -> f64
// CHECK-NOT: obelisk.sv.
