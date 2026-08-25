// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

module {
  obelisk_sim.design @invalid_specify_transition_delays {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9943001 in 0 continuous hierarchy "top.path"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @path(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %snapshot: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9943001 : i64,
                    obelisk_sim.timing_path_rules = [
                      {inputs = ["top.source"], snapshots = ["top.snapshot"],
                       input_lows = array<i64: 0>, input_widths = array<i64: 1>,
                       output_low = 0 : i64, output_width = 1 : i64,
                       output_root_width = 1 : i64,
                       connection_full = false, polarity = 0 : i32,
                       delays = array<i64: 1, 2, 3, 4>,
                       condition_kind = 0 : i32,
                       condition_group = 0 : i32}],
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>]} {
      obelisk_sim.return
    }
  }
}

// CHECK: error: invalid frozen overlapping timing path
