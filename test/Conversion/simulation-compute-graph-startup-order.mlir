// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// Function names deliberately put the initial process before the always
// process lexically. The graph assigns repeating-process fragments first so
// they can establish time-zero sensitivities before an initial process runs.

module {
  obelisk_sim.design @startup_order {
    obelisk_sim.code_unit.decl 9700001 in 0 root_initializer
        hierarchy "root"
    obelisk_sim.code_unit.decl 9700002 in 0 initial
        hierarchy "a_initial"
    obelisk_sim.code_unit.decl 9700003 in 0 always
        hierarchy "z_always"
    obelisk_sim.scope.decl 0

    // CHECK: compute_graph = #obelisk_sim.graph<
    // CHECK-SAME: nodes = [
    // CHECK-SAME: #obelisk_sim.fragment<id = [[INITIAL:[0-9]+]], function = @a_initial
    // CHECK-SAME: #obelisk_sim.fragment<id = [[ROOT:[0-9]+]], function = @root
    // CHECK-SAME: #obelisk_sim.fragment<id = [[ALWAYS:[0-9]+]], function = @z_always
    // CHECK-SAME: kind = spawn
    // CHECK-SAME: #obelisk_sim.edge<source = [[ALWAYS]], target = [[INITIAL]], kind = process_order>
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9700001 : i64} {
      %always = obelisk_sim.spawn @z_always(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %initial = obelisk_sim.spawn @a_initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @a_initial(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9700002 : i64} {
      obelisk_sim.return
    }

    obelisk_sim.func @z_always(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 9700003 : i64} {
      obelisk_sim.return
    }
  }
}
