// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module attributes {obelisk.coverage.metrics = ["line"]} {
  obelisk.sv.symbol.definition @dut_definition attributes {definition_kind = 0 : i32, hierarchical_name = "DUT", name = "DUT", node_id = 0 : i64} {}
  obelisk.sv.symbol.definition @nested_dut_definition attributes {definition_kind = 0 : i32, hierarchical_name = "outer.DUT", name = "DUT", node_id = 1 : i64} {}
  obelisk.sv.symbol.definition @interface_definition attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 2 : i64} {}
  obelisk.sv.symbol.definition @program_definition attributes {definition_kind = 2 : i32, hierarchical_name = "test_program", name = "test_program", node_id = 3 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 4 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 5 : i64} {}
    obelisk.sv.symbol.instance @first attributes {hierarchical_name = "top.first", is_uninstantiated = false, name = "first", node_id = 6 : i64, referenced_path = "DUT", referenced_symbol = @dut_definition} {
      obelisk.sv.symbol.instance_body @first_body attributes {hierarchical_name = "top.first", name = "first_body", node_id = 7 : i64} {}
    }
    obelisk.sv.symbol.instance @second attributes {hierarchical_name = "top.second", is_uninstantiated = false, name = "second", node_id = 8 : i64, referenced_path = "DUT", referenced_symbol = @dut_definition} {
      obelisk.sv.symbol.instance_body @second_body attributes {hierarchical_name = "top.second", name = "second_body", node_id = 9 : i64} {}
    }
    obelisk.sv.symbol.instance @nested attributes {hierarchical_name = "top.nested", is_uninstantiated = false, name = "nested", node_id = 10 : i64, referenced_path = "outer.DUT", referenced_symbol = @nested_dut_definition} {
      obelisk.sv.symbol.instance_body @nested_body attributes {hierarchical_name = "top.nested", name = "colliding_leaf", node_id = 11 : i64} {}
    }
    obelisk.sv.symbol.instance @intf attributes {hierarchical_name = "top.intf", is_uninstantiated = false, name = "intf", node_id = 12 : i64, referenced_path = "bus_if", referenced_symbol = @interface_definition} {
      obelisk.sv.symbol.instance_body @interface_body attributes {hierarchical_name = "top.intf", name = "bus_if", node_id = 13 : i64} {}
    }
    obelisk.sv.symbol.instance @program attributes {hierarchical_name = "top.program", is_uninstantiated = false, name = "program", node_id = 14 : i64, referenced_path = "test_program", referenced_symbol = @program_definition} {
      obelisk.sv.symbol.instance_body @program_body attributes {hierarchical_name = "top.program", name = "test_program", node_id = 15 : i64} {}
    }
  }
}

// CHECK: simulation.scope.decl 0
// CHECK-DAG: simulation.scope.decl {{[0-9]+}} parent 0 hierarchy "top.first" debug "first_body" source_definition "DUT"
// CHECK-DAG: simulation.scope.decl {{[0-9]+}} parent 0 hierarchy "top.second" debug "second_body" source_definition "DUT"
// CHECK-DAG: simulation.scope.decl {{[0-9]+}} parent 0 hierarchy "top.nested" debug "colliding_leaf" source_definition "outer.DUT"
// CHECK-DAG: simulation.scope.decl {{[0-9]+}} parent 0 hierarchy "top.intf" debug "bus_if" coverage_id {{[0-9]+}}
// CHECK-DAG: simulation.scope.decl {{[0-9]+}} parent 0 hierarchy "top.program" debug "test_program" coverage_id {{[0-9]+}}
