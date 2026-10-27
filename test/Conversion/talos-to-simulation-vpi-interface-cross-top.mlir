// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0 vpi=full' | FileCheck %s
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0 vpi=full' | FileCheck %s --check-prefix=VPI
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0 vpi=off' | FileCheck %s

// IEEE 1800-2023 25.9 permits a virtual interface passed into caller.invoke
// to refer to an exact nominal instance under another top module. The receiver
// is an argument, so execution must retain the remote instance dispatch branch.

module {
  talos.sv.symbol.definition @caller_def attributes {
      definition_kind = 0 : i32, hierarchical_name = "caller", name = "caller",
      node_id = 200 : i64} {}

  talos.sv.symbol.definition @if_def attributes {
      definition_kind = 1 : i32, hierarchical_name = "bus_if",
      name = "bus_if", node_id = 0 : i64} {
  }
  talos.sv.symbol.definition @top_def attributes {
      definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
      node_id = 1 : i64} {
  }
  talos.sv.symbol.root @root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64
  } {
    talos.sv.symbol.instance @caller attributes {
        hierarchical_name = "caller", is_uninstantiated = false, name = "caller",
        node_id = 201 : i64, referenced_path = "caller", referenced_symbol = @caller_def} {
      talos.sv.symbol.instance_body @caller_body attributes {
          hierarchical_name = "caller", name = "caller", node_id = 202 : i64} {
      talos.sv.symbol.subroutine @invoke attributes {
          hierarchical_name = "caller.invoke", name = "invoke",
          node_id = 4 : i64,
          semantic_type = !talos.subroutine<(!talos.virtual_interface<@root::@top::@top_body::@bus, "">) -> (), true>,
          subroutine_kind = 1 : i32} {
        talos.sv.statement.list attributes {node_id = 5 : i64} {
          talos.sv.statement.expression_statement attributes {
              node_id = 6 : i64} {
            talos.sv.expression.call attributes {
                argument_count = 0 : i64, callee_name = "ping",
                constraint_restrictions = [], defaulted_arguments = array<i64>,
                has_inline_constraints = false,
                has_iterator_expression = false, has_output_arguments = false,
                has_this_class = true, is_signed = false,
                is_super_class = false, is_system_call = false,
                node_id = 7 : i64, semantic_type = !talos.void,
                subroutine_kind = 0 : i32} {
              talos.sv.expression.named_value attributes {
                  is_signed = false, node_id = 8 : i64,
                  referenced_path = "caller.invoke.vif",
                  referenced_symbol = @root::@caller::@caller_body::@invoke::@vif,
                  semantic_type = !talos.virtual_interface<@root::@top::@top_body::@bus, "">} {
              }
            }
          }
        }
        talos.sv.symbol.formal_argument @vif attributes {
            direction = 0 : i32, hierarchical_name = "caller.invoke.vif",
            name = "vif", node_id = 9 : i64,
            semantic_type = !talos.virtual_interface<@root::@top::@top_body::@bus, "">
        } {
        }
      }
    }
    }
    talos.sv.symbol.instance @top attributes {
        hierarchical_name = "top", is_uninstantiated = false, name = "top",
        node_id = 10 : i64, referenced_path = "top",
        referenced_symbol = @top_def} {
      talos.sv.symbol.instance_body @top_body attributes {
          hierarchical_name = "top", name = "top", node_id = 11 : i64
      } {
        talos.sv.symbol.instance @bus attributes {
            hierarchical_name = "top.bus", is_uninstantiated = false,
            name = "bus", node_id = 12 : i64, referenced_path = "bus_if",
            referenced_symbol = @if_def} {
          talos.sv.symbol.instance_body @bus_body attributes {
              hierarchical_name = "top.bus", name = "bus_if",
              node_id = 13 : i64,
              virtual_interface_identity = @root::@top::@top_body::@bus} {
            talos.sv.symbol.subroutine @ping attributes {
                hierarchical_name = "top.bus.ping", name = "ping",
                node_id = 14 : i64,
                semantic_type = !talos.subroutine<() -> !talos.void, false>,
                subroutine_kind = 0 : i32} {
              talos.sv.statement.list attributes {node_id = 15 : i64} {
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @initial attributes {
            hierarchical_name = "top", node_id = 16 : i64,
            procedure_kind = 0 : i32} {
          talos.sv.statement.expression_statement attributes {
              node_id = 17 : i64} {
            talos.sv.expression.call attributes {
                argument_count = 1 : i64, callee_name = "invoke",
                constraint_restrictions = [],
                defaulted_arguments = array<i64: 0>,
                has_inline_constraints = false,
                has_iterator_expression = false, has_output_arguments = false,
                has_this_class = false, is_signed = false,
                is_super_class = false, is_system_call = false,
                node_id = 18 : i64, referenced_path = "caller.invoke",
                referenced_symbol = @root::@caller::@caller_body::@invoke,
                semantic_type = !talos.void, subroutine_kind = 1 : i32} {
              talos.sv.expression.arbitrary_symbol attributes {
                  is_signed = false, node_id = 19 : i64,
                  referenced_path = "top.bus",
                  referenced_symbol = @root::@top::@top_body::@bus,
                  semantic_type = !talos.virtual_interface<@root::@top::@top_body::@bus, "">} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.scope.decl [[BUS:[0-9]+]] {{.*}} hierarchy "top.bus"
// CHECK: simulation.code_unit.decl [[TARGET:[0-9]+]] in [[BUS]] function hierarchy "top.bus.ping"
// VPI: simulation.scope.decl [[BUS:[0-9]+]] {{.*}} hierarchy "top.bus"
// VPI: simulation.code_unit.decl [[TARGET:[0-9]+]] in [[BUS]] function hierarchy "top.bus.ping"
// VPI: simulation.vpi_interface_member.decl @[[MEMBER:[^ ]+]] of @{{[^ ]+}} name "ping"
// VPI: simulation.vpi_callable.decl [[TARGET]] signature () -> () directions [] member @[[MEMBER]]
// VPI: simulation.vpi_interface_call.dispatch @{{[^ ]+}} member @[[MEMBER]] access 0
// CHECK-LABEL: simulation.func private @{{[^ (]+}}
// CHECK-SAME: simulation.hierarchical_name = "caller.invoke"
// CHECK: simulation.virtual_interface.scope
// CHECK: arith.cmpi eq
// CHECK: simulation.call @[[PING:[^ (]+]]
// CHECK: simulation.func private @[[PING]]
// CHECK-SAME: code_unit_id = [[TARGET]]
// CHECK-SAME: simulation.hierarchical_name = "top.bus.ping"
