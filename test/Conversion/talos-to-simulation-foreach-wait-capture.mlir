// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

module {
  talos.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top_def} {
      talos.sv.symbol.instance_body @top_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        talos.sv.symbol.variable @values attributes {hierarchical_name = "top.values", lifetime = 1 : i32, name = "values", node_id = 5 : i64, semantic_type = !talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>} {}
        talos.sv.symbol.statement_block @loop_scope attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 6 : i64} {
          talos.sv.symbol.iterator @j attributes {array_type = !talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>, hierarchical_name = "top.j", index_method_name = "", is_const, name = "j", node_id = 7 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.foreach_loop attributes {loop_dimensions = [{has_iterator = true, has_static_range = false, iterator_path = "top.j", iterator_symbol = @root::@top::@top_body::@loop_scope::@j, iterator_type = !talos.integral<32, true, false, 31 : 0, int>}], node_id = 9 : i64} {
            talos.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "top.values", referenced_symbol = @root::@top::@top_body::@values, semantic_type = !talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>} {}
            talos.sv.statement.wait attributes {node_id = 11 : i64} {
              talos.sv.expression.element_select attributes {node_id = 12 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                talos.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.values", referenced_symbol = @root::@top::@top_body::@values, semantic_type = !talos.dynarray<!talos.integral<32, true, false, 31 : 0, int>>} {}
                talos.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "top.j", referenced_symbol = @root::@top::@top_body::@loop_scope::@j, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
              }
              talos.sv.statement.empty attributes {node_id = 15 : i64} {}
            }
          }
        }
      }
    }
  }
}

// CHECK-COUNT-1: [[ITER:%.*]] = simulation.ref.alloc {{.*}} : i32 -> !simulation.ref<i32>
// The same iterator allocation is restored through the loop header and body;
// a resumed iteration cannot use the initial activation's scratch register.
// CHECK: cf.br ^[[HEADER:bb[0-9]+]](%{{.*}}, [[ITER]], %{{.*}} : i64, !simulation.ref<i32>, i64)
// CHECK: ^[[HEADER]](%{{.*}}: i64, [[HEADER_ITER:%[^, ]+]]: !simulation.ref<i32>,
// CHECK: cf.cond_br %{{.*}}, ^[[BODY:bb[0-9]+]]([[HEADER_ITER]],
// CHECK: ^[[BODY]]([[BODY_ITER:%[^, ]+]]: !simulation.ref<i32>,
// CHECK: simulation.ref.store {{.*}} to [[BODY_ITER]] : i32, !simulation.ref<i32>
// CHECK: cf.cond_br %{{.*}}, ^{{bb[0-9]+}}({{.*}}), ^[[WAIT:bb[0-9]+]]([[BODY_ITER]],
// CHECK: ^[[WAIT]]([[WAIT_ITER:%[^, ]+]]: !simulation.ref<i32>,
// CHECK: simulation.observer.bind @observer_
// CHECK-SAME: [[WAIT_ITER]]
// CHECK-SAME: captures 2 : !simulation.observer<i1>
// CHECK: simulation.func private @observer_
// CHECK-SAME: !simulation.ref<i32>
// CHECK-SAME: -> i1
// CHECK: simulation.ref.load {{.*}} : !simulation.ref<i32> -> i32
// CHECK-NOT: talos.sv.
