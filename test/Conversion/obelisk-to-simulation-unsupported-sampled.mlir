// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "unsupported_sampled", name = "unsupported_sampled", node_id = 0 : i64, sym_name = "s0.unsupported_sampled"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "unsupported_sampled", is_uninstantiated = false, name = "unsupported_sampled", node_id = 3 : i64, referenced_path = "unsupported_sampled", referenced_symbol = @s0.unsupported_sampled, sym_name = "s3.unsupported_sampled"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "unsupported_sampled", name = "unsupported_sampled", node_id = 4 : i64, sym_name = "s4.unsupported_sampled"} {
        obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "unsupported_sampled.value", name = "value", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.value"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "unsupported_sampled.value", lifetime = 1 : i32, name = "value", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "unsupported_sampled", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = false, is_final = false, node_id = 8 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$sampled", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.unsupported_sampled", system_scope_path = "unsupported_sampled", system_scope_symbol = @s1.$root::@s3.unsupported_sampled::@s4.unsupported_sampled} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "unsupported_sampled.value", referenced_symbol = @s1.$root::@s3.unsupported_sampled::@s4.unsupported_sampled::@s6.value, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 11 : i64} {
            }
          }
        }
      }
    }
  }
}

// CHECK: $sampled requires concurrent assertion Preponed sampling, which is not executable yet
