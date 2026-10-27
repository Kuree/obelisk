// RUN: talos-opt %s --pass-pipeline='builtin.module(talos-sim-prepare-unit-lowering,simulation.design(simulation.func(talos-sim-lower-unit)))' | FileCheck %s

!logic1 = !talos.integral<1, false, true, 0 : 0, logic>
!logic3 = !talos.integral<3, false, true, 2 : 0, logic>

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
                      #simulation.timing_path_rule<inputs = ["top.source"], snapshots = ["top.snapshot0"], polarity = 0, delays = [7],
                        selection = #simulation.timing_path_selection<inputLows = [0], inputWidths = [1], outputLow = 0, outputWidth = 1, outputRootWidth = 1, driverNode = 2>,
                        condition = #simulation.timing_path_condition<kind = none, group = 0>>,
                      #simulation.timing_path_rule<inputs = ["top.source"], snapshots = ["top.snapshot0"], polarity = 0, delays = [7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7],
                        selection = #simulation.timing_path_selection<inputLows = [0], inputWidths = [1], outputLow = 0, outputWidth = 1, outputRootWidth = 1, driverNode = 2>,
                        condition = #simulation.timing_path_condition<kind = none, group = 1>>,
                      #simulation.timing_path_rule<inputs = ["top.source"], snapshots = ["top.snapshot1"], polarity = 1, delays = [2, 5],
                        selection = #simulation.timing_path_selection<inputLows = [1], inputWidths = [1], outputLow = 0, outputWidth = 1, outputRootWidth = 1, driverNode = 3>,
                        condition = #simulation.timing_path_condition<kind = none, group = 2>>,
                      #simulation.timing_path_rule<inputs = ["top.source"], snapshots = ["top.snapshot1"], polarity = 2, delays = [2, 5, 2, 2, 5, 5, 2, 2, 5, 5, 5, 2],
                        selection = #simulation.timing_path_selection<inputLows = [1], inputWidths = [1], outputLow = 0, outputWidth = 1, outputRootWidth = 1, driverNode = 3>,
                        condition = #simulation.timing_path_condition<kind = none, group = 3>>,
                      #simulation.timing_path_rule<inputs = ["top.source"], snapshots = ["top.snapshot2"], polarity = 0, delays = [2, 5, 3],
                        selection = #simulation.timing_path_selection<inputLows = [2], inputWidths = [1], outputLow = 0, outputWidth = 1, outputRootWidth = 1, driverNode = 4>,
                        condition = #simulation.timing_path_condition<kind = none, group = 4>>,
                      #simulation.timing_path_rule<inputs = ["top.source"], snapshots = ["top.snapshot2"], polarity = 0, delays = [2, 5, 3, 2, 3, 5, 2, 2, 3, 5, 3, 2],
                        selection = #simulation.timing_path_selection<inputLows = [2], inputWidths = [1], outputLow = 0, outputWidth = 1, outputRootWidth = 1, driverNode = 4>,
                        condition = #simulation.timing_path_condition<kind = none, group = 5>>],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.one", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.two", argument = 2, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.three", argument = 3, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.source", argument = 4, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot0", argument = 5, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot1", argument = 6, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot2", argument = 7, kind = direct, copyOut = false>]} {
      talos.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic3} {
        talos.sv.expression.concatenation attributes {node_id = 10 : i64,
                                                         semantic_type = !logic3} {
          talos.sv.expression.named_value attributes {node_id = 4 : i64,
              referenced_path = "top.three", referenced_symbol = @three,
              semantic_type = !logic1} {}
          talos.sv.expression.named_value attributes {node_id = 3 : i64,
              referenced_path = "top.two", referenced_symbol = @two,
              semantic_type = !logic1} {}
          talos.sv.expression.named_value attributes {node_id = 2 : i64,
              referenced_path = "top.one", referenced_symbol = @one,
              semantic_type = !logic1} {}
        }
        talos.sv.expression.named_value attributes {node_id = 5 : i64,
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
