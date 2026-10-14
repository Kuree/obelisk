// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>

module {
  simulation.design @partial_specify_lowering {
    simulation.scope.decl 0
    simulation.code_unit.decl 9930001 in 0 continuous
        hierarchy "partial_specify_lowering.path"
    simulation.code_unit.decl 9930002 in 0 observer
        hierarchy "partial_specify_lowering.condition"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.net.decl 1 in 0 : !simulation.logic<4> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design

    simulation.func private @condition(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 2 : i64}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9930002 : i64,
                    simulation.lowered} {
      %value = simulation.net.read %condition :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %truth = simulation.logic.is_true %value : !simulation.logic<1>
      simulation.return %truth : i1
    }

    simulation.func @path(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<4>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %source: !simulation.net<!simulation.logic<4>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64},
        %snapshot: !simulation.ref<!simulation.logic<4>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %condition: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9930001 : i64,
                    simulation.timing_path_rules = [
                      #simulation.timing_path_rule<inputs = ["top.source"], snapshots = ["top.snapshot"], polarity = 1, delays = [1, 2, 3, 4, 5, 6],
                        selection = #simulation.timing_path_selection<inputLows = [2], inputWidths = [2], outputLow = 0, outputWidth = 2, outputRootWidth = 4>,
                        condition = #simulation.timing_path_condition<kind = if, group = 0, evaluator = @condition, captures = ["top.condition"]>>,
                      #simulation.timing_path_rule<inputs = ["top.source"], snapshots = ["top.snapshot"], polarity = 2, delays = [9],
                        selection = #simulation.timing_path_selection<inputLows = [2], inputWidths = [2], outputLow = 0, outputWidth = 2, outputRootWidth = 4>,
                        condition = #simulation.timing_path_condition<kind = ifnone, group = 0>>,
                      #simulation.timing_path_rule<inputs = ["top.source", "top.source"], snapshots = ["top.snapshot", "top.snapshot"], polarity = 0, delays = [1, 4, 6],
                        selection = #simulation.timing_path_selection<inputLows = [0, 3], inputWidths = [1, 1], outputLow = 2, outputWidth = 2, outputRootWidth = 4, full = true>,
                        condition = #simulation.timing_path_condition<kind = none, group = 1>>],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.condition", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic4} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic4} {}
        obelisk.sv.expression.named_value attributes {
            node_id = 3 : i64, referenced_path = "top.source",
            referenced_symbol = @source, semantic_type = !logic4} {}
      }
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @path
// CHECK-COUNT-1: simulation.logic.case_difference_mask
// CHECK-COUNT-1: simulation.ref.store
// CHECK: arith.shrui
// CHECK: arith.trunci
// CHECK: simulation.call @condition(%arg0, %arg4)
// CHECK: arith.xori
// CHECK: simulation.driver.read
// CHECK: simulation.driver.drive_inertial_path
// CHECK-SAME: group 0 of 7
// CHECK: simulation.driver.drive_inertial_path{{.*}}group 6 of 7
// CHECK: simulation.suspend.change %arg2
