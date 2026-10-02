// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

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
                      {inputs = ["top.source"], snapshots = ["top.snapshot"],
                       input_lows = array<i64: 0>, input_widths = array<i64: 1>,
                       output_low = 0 : i64, output_width = 1 : i64,
                       output_root_width = 1 : i64,
                       connection_full = false, polarity = 0 : i32,
                       delays = array<i64: 1, 2, 3, 4>,
                       condition_kind = 0 : i32,
                       condition_group = 0 : i32}],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>]} {
      simulation.return
    }
  }
}

// CHECK: error: invalid frozen overlapping timing path
