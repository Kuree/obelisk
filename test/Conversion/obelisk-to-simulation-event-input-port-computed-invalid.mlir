// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, name = "child", node_id = 0 : i64, sym_name = "child"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, name = "top", node_id = 1 : i64, sym_name = "top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "top_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "top_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 5 : i64, semantic_type = !obelisk.event, sym_name = "a"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 6 : i64, semantic_type = !obelisk.event, sym_name = "b"} {}
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.called", name = "called", node_id = 7 : i64, referenced_path = "child", referenced_symbol = @child, sym_name = "called"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "wake", formal_ordinal = 0 : i64, formal_path = "top.called.wake", formal_symbol = @root::@top_i::@top_b::@called::@called_b::@called_p, formal_type = !obelisk.event, internal_path = "top.called.wake", internal_symbol = @root::@top_i::@top_b::@called::@called_b::@called_v, is_ansi = true, is_net = false, node_id = 8 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$global_clock", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.event, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @root::@top_i::@top_b} {}
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.called", name = "child", node_id = 10 : i64, sym_name = "called_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.called.wake", name = "wake", node_id = 11 : i64, semantic_type = !obelisk.event, sym_name = "called_p"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.called.wake", lifetime = 1 : i32, name = "wake", node_id = 12 : i64, semantic_type = !obelisk.event, sym_name = "called_v"} {}
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.effect", name = "effect", node_id = 13 : i64, referenced_path = "child", referenced_symbol = @child, sym_name = "effect"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 0 : i32, formal_name = "wake", formal_ordinal = 0 : i64, formal_path = "top.effect.wake", formal_symbol = @root::@top_i::@top_b::@effect::@effect_b::@effect_p, formal_type = !obelisk.event, internal_path = "top.effect.wake", internal_symbol = @root::@top_i::@top_b::@effect::@effect_b::@effect_v, is_ansi = true, is_net = false, node_id = 14 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 15 : i64, semantic_type = !obelisk.event} {
              obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.a", referenced_symbol = @root::@top_i::@top_b::@a, semantic_type = !obelisk.event} {}
              obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "top.b", referenced_symbol = @root::@top_i::@top_b::@b, semantic_type = !obelisk.event} {}
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.effect", name = "child", node_id = 18 : i64, sym_name = "effect_b", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "top.effect.wake", name = "wake", node_id = 19 : i64, semantic_type = !obelisk.event, sym_name = "effect_p"} {}
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.effect.wake", lifetime = 1 : i32, name = "wake", node_id = 20 : i64, semantic_type = !obelisk.event, sym_name = "effect_v"} {}
          }
        }
      }
    }
  }
}

// CHECK-DAG: computed event input startup dependency has unresolved call effects
// CHECK-DAG: computed event input actual is not a side-effect-free event expression
