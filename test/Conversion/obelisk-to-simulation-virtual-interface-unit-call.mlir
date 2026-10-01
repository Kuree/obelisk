// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A compilation-unit subroutine, like a class method, has no enclosing
// design instance. Its virtual-interface call can still select any compatible
// elaborated interface instance.

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
    obelisk.sv.symbol.compilation_unit @unit attributes {
        hierarchical_name = "$unit", node_id = 3 : i64} {
      obelisk.sv.symbol.subroutine @invoke attributes {
          hierarchical_name = "$unit.invoke", name = "invoke",
          node_id = 4 : i64,
          semantic_type = !obelisk.subroutine<(!obelisk.virtual_interface<@root::@top::@top_body::@bus, "">) -> (), true>,
          subroutine_kind = 1 : i32} {
        obelisk.sv.statement.list attributes {node_id = 5 : i64} {
          obelisk.sv.statement.expression_statement attributes {
              node_id = 6 : i64} {
            obelisk.sv.expression.call attributes {
                argument_count = 0 : i64, callee_name = "ping",
                constraint_restrictions = [], defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false, has_output_arguments = false,
                has_this_class = true, is_signed = false,
                is_super_class = false, is_system_call = false,
                node_id = 7 : i64, semantic_type = !obelisk.void,
                subroutine_kind = 0 : i32} {
              obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 8 : i64,
                  referenced_path = "$unit.invoke.vif",
                  referenced_symbol = @root::@unit::@invoke::@vif,
                  semantic_type = !obelisk.virtual_interface<@root::@top::@top_body::@bus, "">} {
              }
            }
          }
        }
        obelisk.sv.symbol.formal_argument @vif attributes {
            direction = 0 : i32, hierarchical_name = "$unit.invoke.vif",
            name = "vif", node_id = 9 : i64,
            semantic_type = !obelisk.virtual_interface<@root::@top::@top_body::@bus, "">
        } {
        }
      }
    }
    obelisk.sv.symbol.instance @top attributes {
        hierarchical_name = "top", is_uninstantiated = false, name = "top",
        node_id = 10 : i64, referenced_path = "top",
        referenced_symbol = @top_def} {
      obelisk.sv.symbol.instance_body @top_body attributes {
          hierarchical_name = "top", name = "top", node_id = 11 : i64
      } {
        obelisk.sv.symbol.instance @bus attributes {
            hierarchical_name = "top.bus", is_uninstantiated = false,
            name = "bus", node_id = 12 : i64, referenced_path = "bus_if",
            referenced_symbol = @if_def} {
          obelisk.sv.symbol.instance_body @bus_body attributes {
              hierarchical_name = "top.bus", name = "bus_if",
              node_id = 13 : i64,
              virtual_interface_identity = @root::@top::@top_body::@bus} {
            obelisk.sv.symbol.subroutine @ping attributes {
                hierarchical_name = "top.bus.ping", name = "ping",
                node_id = 14 : i64,
                semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
                subroutine_kind = 0 : i32} {
              obelisk.sv.statement.list attributes {node_id = 15 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @initial attributes {
            hierarchical_name = "top", node_id = 16 : i64,
            procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {
              node_id = 17 : i64} {
            obelisk.sv.expression.call attributes {
                argument_count = 1 : i64, callee_name = "invoke",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0>,
                has_inline_constraints = false,
                has_iterator_expression = false, has_output_arguments = false,
                has_this_class = false, is_signed = false,
                is_super_class = false, is_system_call = false,
                node_id = 18 : i64, referenced_path = "$unit.invoke",
                referenced_symbol = @root::@unit::@invoke,
                semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
              obelisk.sv.expression.arbitrary_symbol attributes {
                  is_signed = false, node_id = 19 : i64,
                  referenced_path = "top.bus",
                  referenced_symbol = @root::@top::@top_body::@bus,
                  semantic_type = !obelisk.virtual_interface<@root::@top::@top_body::@bus, "">} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.scope.decl [[BUS:[0-9]+]] {{.*}} hierarchy "top.bus"
// CHECK-LABEL: simulation.func private @unit_
// CHECK: simulation.virtual_interface.scope
// CHECK: arith.cmpi eq
// CHECK: simulation.call
// CHECK: virtual interface call used a null or invalid handle.
