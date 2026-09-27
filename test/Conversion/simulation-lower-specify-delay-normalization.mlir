// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!logic3 = !obelisk.integral<3, false, true, 2 : 0, logic>

module {
  simulation.design @specify_delay_normalization {
    simulation.scope.decl 0
    simulation.code_unit.decl 9942001 in 0 continuous hierarchy "top.path"
    simulation.storage.decl 0 in 0 : !simulation.logic<3> design
    simulation.storage.decl 1 in 0 : !simulation.logic<3> design
    simulation.storage.decl 2 in 0 : !simulation.logic<3> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.net.decl 3 in 0 : !simulation.logic<3> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design

    simulation.func @path(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %one: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %two: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64},
        %three: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64},
        %source: !simulation.net<!simulation.logic<3>>
            {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 3 : i64},
        %snapshot0: !simulation.ref<!simulation.logic<3>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %snapshot1: !simulation.ref<!simulation.logic<3>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %snapshot2: !simulation.ref<!simulation.logic<3>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9942001 : i64,
                    simulation.timing_path_rules = [
                      {inputs = ["top.source"], snapshots = ["top.snapshot0"],
                       input_lows = array<i64: 0>, input_widths = array<i64: 1>,
                       output_low = 0 : i64, output_width = 1 : i64,
                       output_root_width = 1 : i64, driver_node_id = 2 : i64,
                       connection_full = false, polarity = 0 : i32,
                       delays = array<i64: 7>, condition_kind = 0 : i32,
                       condition_group = 0 : i32},
                      {inputs = ["top.source"], snapshots = ["top.snapshot0"],
                       input_lows = array<i64: 0>, input_widths = array<i64: 1>,
                       output_low = 0 : i64, output_width = 1 : i64,
                       output_root_width = 1 : i64, driver_node_id = 2 : i64,
                       connection_full = false, polarity = 0 : i32,
                       delays = array<i64: 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7>,
                       condition_kind = 0 : i32, condition_group = 1 : i32},
                      {inputs = ["top.source"], snapshots = ["top.snapshot1"],
                       input_lows = array<i64: 1>, input_widths = array<i64: 1>,
                       output_low = 0 : i64, output_width = 1 : i64,
                       output_root_width = 1 : i64, driver_node_id = 3 : i64,
                       connection_full = false, polarity = 1 : i32,
                       delays = array<i64: 2, 5>, condition_kind = 0 : i32,
                       condition_group = 2 : i32},
                      {inputs = ["top.source"], snapshots = ["top.snapshot1"],
                       input_lows = array<i64: 1>, input_widths = array<i64: 1>,
                       output_low = 0 : i64, output_width = 1 : i64,
                       output_root_width = 1 : i64, driver_node_id = 3 : i64,
                       connection_full = false, polarity = 2 : i32,
                       delays = array<i64: 2, 5, 2, 2, 5, 5, 2, 2, 5, 5, 5, 2>,
                       condition_kind = 0 : i32, condition_group = 3 : i32},
                      {inputs = ["top.source"], snapshots = ["top.snapshot2"],
                       input_lows = array<i64: 2>, input_widths = array<i64: 1>,
                       output_low = 0 : i64, output_width = 1 : i64,
                       output_root_width = 1 : i64, driver_node_id = 4 : i64,
                       connection_full = false, polarity = 0 : i32,
                       delays = array<i64: 2, 5, 3>, condition_kind = 0 : i32,
                       condition_group = 4 : i32},
                      {inputs = ["top.source"], snapshots = ["top.snapshot2"],
                       input_lows = array<i64: 2>, input_widths = array<i64: 1>,
                       output_low = 0 : i64, output_width = 1 : i64,
                       output_root_width = 1 : i64, driver_node_id = 4 : i64,
                       connection_full = false, polarity = 0 : i32,
                       delays = array<i64: 2, 5, 3, 2, 3, 5, 2, 2, 3, 5, 3, 2>,
                       condition_kind = 0 : i32, condition_group = 5 : i32}],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.one", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.two", argument = 2, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.three", argument = 3, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.source", argument = 4, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot0", argument = 5, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot1", argument = 6, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot2", argument = 7, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic3} {
        obelisk.sv.expression.concatenation attributes {node_id = 10 : i64,
                                                         semantic_type = !logic3} {
          obelisk.sv.expression.named_value attributes {node_id = 4 : i64,
              referenced_path = "top.three", referenced_symbol = @three,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 3 : i64,
              referenced_path = "top.two", referenced_symbol = @two,
              semantic_type = !logic1} {}
          obelisk.sv.expression.named_value attributes {node_id = 2 : i64,
              referenced_path = "top.one", referenced_symbol = @one,
              semantic_type = !logic1} {}
        }
        obelisk.sv.expression.named_value attributes {node_id = 5 : i64,
            referenced_path = "top.source", referenced_symbol = @source,
            semantic_type = !logic3} {}
      }
      simulation.return
    }
  }
}

// Each compact batch has exactly the distinct delays in the short tuple and
// its explicitly derived twelve-class equivalent.
// CHECK-LABEL: simulation.func @path
// CHECK: simulation.driver.read
// CHECK: site 9942001 : {{[0-9]+}} group 0 of 3
// CHECK: site 9942001 : {{[0-9]+}} group 2 of 3
// CHECK: simulation.driver.read
// CHECK: site 9942001 : {{[0-9]+}} group 0 of 2
// CHECK: site 9942001 : {{[0-9]+}} group 1 of 2
// CHECK: simulation.driver.read
// CHECK: site 9942001 : {{[0-9]+}} group 0 of 1
