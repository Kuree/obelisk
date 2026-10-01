// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top_def} {
      obelisk.sv.symbol.instance_body @top_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @values attributes {hierarchical_name = "top.values", lifetime = 1 : i32, name = "values", node_id = 5 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>} {}
        obelisk.sv.symbol.statement_block @loop_scope attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 6 : i64} {
          obelisk.sv.symbol.iterator @j attributes {array_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>, hierarchical_name = "top.j", index_method_name = "", is_const, name = "j", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.foreach_loop attributes {loop_dimensions = [{has_iterator = true, has_static_range = false, iterator_path = "top.j", iterator_symbol = @root::@top::@top_body::@loop_scope::@j, iterator_type = !obelisk.integral<32, true, false, 31 : 0, int>}], node_id = 9 : i64} {
            obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "top.values", referenced_symbol = @root::@top::@top_body::@values, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>} {}
            obelisk.sv.statement.wait attributes {node_id = 11 : i64} {
              obelisk.sv.expression.element_select attributes {node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.values", referenced_symbol = @root::@top::@top_body::@values, semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>} {}
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.j", referenced_symbol = @root::@top::@top_body::@loop_scope::@j, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
              }
              obelisk.sv.statement.empty attributes {node_id = 15 : i64} {}
            }
          }
        }
      }
    }
  }
}

// CHECK-COUNT-1: [[ITER:%.*]] = simulation.ref.alloc {{.*}} : i32 -> !simulation.ref<i32>
// CHECK: simulation.ref.store {{.*}} to [[ITER]] : i32, !simulation.ref<i32>
// CHECK: simulation.observer.bind @observer_
// CHECK-SAME: [[ITER]]
// CHECK-SAME: captures 2 : !simulation.observer<i1>
// CHECK: simulation.func private @observer_
// CHECK-SAME: !simulation.ref<i32>
// CHECK-SAME: -> i1
// CHECK: simulation.ref.load {{.*}} : !simulation.ref<i32> -> i32
// CHECK-NOT: obelisk.sv.
