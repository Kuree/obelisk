// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' \
// RUN:   | FileCheck %s

module {
  talos.sv.symbol.definition @s0.top attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64
  } {}
  talos.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    talos.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit", node_id = 2 : i64
    } {}
    talos.sv.symbol.instance @s3.top attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 3 : i64, referenced_path = "top",
      referenced_symbol = @s0.top
    } {
      talos.sv.symbol.instance_body @s4.top attributes {
        hierarchical_name = "top", name = "top", node_id = 4 : i64
      } {
        talos.sv.symbol.variable @s5.source attributes {
          hierarchical_name = "top.source", lifetime = 1 : i32,
          name = "source", node_id = 5 : i64,
          semantic_type = !talos.dynarray<!talos.dynarray<!talos.integral<8, false, true, 7 : 0, logic>>>
        } {}
        talos.sv.symbol.variable @s6.result attributes {
          hierarchical_name = "top.result", lifetime = 1 : i32,
          name = "result", node_id = 6 : i64,
          semantic_type = !talos.integral<32, false, true, 31 : 0, logic>
        } {
          talos.sv.expression.conversion attributes {
            is_implicit = false, is_signed = false, node_id = 7 : i64,
            semantic_type = !talos.integral<32, false, true, 31 : 0, logic>
          } {
            talos.sv.expression.named_value attributes {
              is_signed = false, node_id = 8 : i64,
              referenced_path = "top.source",
              referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.source,
              semantic_type = !talos.dynarray<!talos.dynarray<!talos.integral<8, false, true, 7 : 0, logic>>>
            } {}
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[SOURCE:.*]] = simulation.ref.load
// CHECK: %[[RESULT:.*]], %[[MATCHED:.*]], %[[WATCH:.*]] = simulation.recursive.export_bitstream %[[SOURCE]]
// CHECK-SAME: (!simulation.dynamic_array<!simulation.dynamic_array<
// CHECK-SAME: -> (!simulation.logic<32>, i1, !simulation.managed_watch)
// CHECK: cf.cond_br %[[MATCHED]], ^[[ACCEPTED:.*]], ^[[REJECTED:.*]]
// CHECK: ^[[REJECTED]]:
// CHECK: bit-stream cast source and destination widths differ
