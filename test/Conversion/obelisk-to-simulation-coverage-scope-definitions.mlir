// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module attributes {obelisk.coverage.metrics = ["line"]} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "DUT", name = "DUT", node_id = 0 : i64, sym_name = "dut_definition"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "outer.DUT", name = "DUT", node_id = 1 : i64, sym_name = "nested_dut_definition"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 2 : i64, sym_name = "interface_definition"} {}
  obelisk.sv.symbol.definition attributes {definition_kind = 2 : i32, hierarchical_name = "test_program", name = "test_program", node_id = 3 : i64, sym_name = "program_definition"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 4 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 5 : i64, sym_name = "unit"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top.first", is_uninstantiated = false, name = "first", node_id = 6 : i64, referenced_path = "DUT", referenced_symbol = @dut_definition, sym_name = "first"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.first", name = "first_body", node_id = 7 : i64, sym_name = "first_body"} {}
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top.second", is_uninstantiated = false, name = "second", node_id = 8 : i64, referenced_path = "DUT", referenced_symbol = @dut_definition, sym_name = "second"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.second", name = "second_body", node_id = 9 : i64, sym_name = "second_body"} {}
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top.nested", is_uninstantiated = false, name = "nested", node_id = 10 : i64, referenced_path = "outer.DUT", referenced_symbol = @nested_dut_definition, sym_name = "nested"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.nested", name = "colliding_leaf", node_id = 11 : i64, sym_name = "nested_body"} {}
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top.intf", is_uninstantiated = false, name = "intf", node_id = 12 : i64, referenced_path = "bus_if", referenced_symbol = @interface_definition, sym_name = "intf"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.intf", name = "bus_if", node_id = 13 : i64, sym_name = "interface_body"} {}
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top.program", is_uninstantiated = false, name = "program", node_id = 14 : i64, referenced_path = "test_program", referenced_symbol = @program_definition, sym_name = "program"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.program", name = "test_program", node_id = 15 : i64, sym_name = "program_body"} {}
    }
  }
}

// CHECK: obelisk_sim.scope.decl 0
// CHECK-DAG: obelisk_sim.scope.decl {{[0-9]+}} parent 0 hierarchy "top.first" debug "first_body" source_definition "DUT"
// CHECK-DAG: obelisk_sim.scope.decl {{[0-9]+}} parent 0 hierarchy "top.second" debug "second_body" source_definition "DUT"
// CHECK-DAG: obelisk_sim.scope.decl {{[0-9]+}} parent 0 hierarchy "top.nested" debug "colliding_leaf" source_definition "outer.DUT"
// CHECK-DAG: obelisk_sim.scope.decl {{[0-9]+}} parent 0 hierarchy "top.intf" debug "bus_if" coverage_id {{[0-9]+}}
// CHECK-DAG: obelisk_sim.scope.decl {{[0-9]+}} parent 0 hierarchy "top.program" debug "test_program" coverage_id {{[0-9]+}}
