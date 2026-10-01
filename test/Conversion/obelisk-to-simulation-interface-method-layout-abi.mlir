// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s

// Interface instances that Slang considers the same virtual-interface type
// can still give their nested typedefs instance-qualified source names. Their
// method ABI is the normalized layout, not that nominal source spelling.

module {
  obelisk.sv.symbol.definition @if_def attributes {
      definition_kind = 1 : i32, hierarchical_name = "bus_if",
      name = "bus_if", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.definition @top_def attributes {
      definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
      node_id = 1 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64
  } {
    obelisk.sv.symbol.instance @top_instance attributes {
        hierarchical_name = "top", is_uninstantiated = false, name = "top",
        node_id = 3 : i64, referenced_path = "top",
        referenced_symbol = @top_def} {
      obelisk.sv.symbol.instance_body @top_body attributes {
          hierarchical_name = "top", name = "top", node_id = 4 : i64
      } {
        obelisk.sv.symbol.instance @a attributes {
            hierarchical_name = "top.a", is_uninstantiated = false,
            name = "a", node_id = 5 : i64, referenced_path = "bus_if",
            referenced_symbol = @if_def} {
          obelisk.sv.symbol.instance_body @a_body attributes {
              hierarchical_name = "top.a", name = "bus_if",
              node_id = 6 : i64,
              virtual_interface_identity = @root::@top_instance::@top_body::@a} {
            obelisk.sv.symbol.subroutine @a_consume attributes {
                hierarchical_name = "top.a.consume", name = "consume",
                node_id = 7 : i64,
                semantic_type = !obelisk.subroutine<(!obelisk.source_aggregate<"top.a", true, false, false, false, false, false, 8, 8, 8, 0, [{name = "value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<8, false, false, 7 : 0, bit>}]>) -> (), true>,
                subroutine_kind = 1 : i32} {
              obelisk.sv.statement.list attributes {node_id = 8 : i64} {
              }
              obelisk.sv.symbol.formal_argument @a_item attributes {
                  direction = 0 : i32,
                  hierarchical_name = "top.a.consume.item", lifetime = 1 : i32,
                  name = "item", node_id = 9 : i64,
                  semantic_type = !obelisk.source_aggregate<"top.a", true, false, false, false, false, false, 8, 8, 8, 0, [{name = "value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<8, false, false, 7 : 0, bit>}]>
              } {
              }
            }
          }
        }
        obelisk.sv.symbol.instance @b attributes {
            hierarchical_name = "top.b", is_uninstantiated = false,
            name = "b", node_id = 10 : i64, referenced_path = "bus_if",
            referenced_symbol = @if_def} {
          obelisk.sv.symbol.instance_body @b_body attributes {
              hierarchical_name = "top.b", name = "bus_if",
              node_id = 11 : i64,
              virtual_interface_identity = @root::@top_instance::@top_body::@a} {
            obelisk.sv.symbol.subroutine @b_consume attributes {
                hierarchical_name = "top.b.consume", name = "consume",
                node_id = 12 : i64,
                semantic_type = !obelisk.subroutine<(!obelisk.source_aggregate<"top.b", true, false, false, false, false, false, 8, 8, 8, 0, [{name = "value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<8, false, false, 7 : 0, bit>}]>) -> (), true>,
                subroutine_kind = 1 : i32} {
              obelisk.sv.statement.list attributes {node_id = 13 : i64} {
              }
              obelisk.sv.symbol.formal_argument @b_item attributes {
                  direction = 0 : i32,
                  hierarchical_name = "top.b.consume.item", lifetime = 1 : i32,
                  name = "item", node_id = 14 : i64,
                  semantic_type = !obelisk.source_aggregate<"top.b", true, false, false, false, false, false, 8, 8, 8, 0, [{name = "value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.integral<8, false, false, 7 : 0, bit>}]>
              } {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-DAG: simulation.code_unit.decl {{[0-9]+}} {{.*}} hierarchy "top.a.consume"
// CHECK-DAG: simulation.code_unit.decl {{[0-9]+}} {{.*}} hierarchy "top.b.consume"
// CHECK-NOT: virtual-interface call candidates have incompatible subroutine ABIs
