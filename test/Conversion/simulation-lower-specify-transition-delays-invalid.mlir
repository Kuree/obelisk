// RUN: not talos-opt %s --pass-pipeline='builtin.module(talos-sim-prepare-unit-lowering,simulation.design(simulation.func(talos-sim-lower-unit)))' 2>&1 | FileCheck %s

module {
  simulation.design @invalid_specify_transition_delays {
    simulation.scope.decl 0
    simulation.code_unit.decl 9943001 in 0 continuous hierarchy "top.path"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design

    simulation.func @path(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %out: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %source: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64},
        %snapshot: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9943001 : i64,
                    simulation.timing_path_rules = [
                      #simulation.timing_path_rule<inputs = ["top.source"], snapshots = ["top.snapshot"], polarity = 0, delays = [1, 2, 3, 4],
                        selection = #simulation.timing_path_selection<inputLows = [0], inputWidths = [1], outputLow = 0, outputWidth = 1, outputRootWidth = 1>,
                        condition = #simulation.timing_path_condition<kind = none, group = 0>>],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>]} {
      simulation.return
    }
  }
}

// CHECK: error: timing path delays are malformed
