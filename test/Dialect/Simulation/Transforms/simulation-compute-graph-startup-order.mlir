// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-plan-static-superstep))' | FileCheck %s --check-prefix=SUPERSTEP
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{workers=2},obelisk-sim-verify-compute-graph,obelisk-sim-plan-static-superstep{missed-remarks=true}))' > %t.workers 2> %t.workers-remarks
// RUN: FileCheck %s --check-prefix=NO-SUPERSTEP < %t.workers
// RUN: FileCheck %s --check-prefix=WORKER-REMARK < %t.workers-remarks

// Function names deliberately put the initial process before the always and
// internal port processes lexically. The graph assigns startup infrastructure
// first so it can establish time-zero sensitivities and values before an
// initial process runs.

module {
  // NO-SUPERSTEP-NOT: obelisk_sim.static_superstep
  // WORKER-REMARK: remark: static superstep not planned: static supersteps require one worker
  obelisk_sim.design @startup_order {
    obelisk_sim.code_unit.decl 9700001 in 0 root_initializer
        hierarchy "root"
    obelisk_sim.code_unit.decl 9700002 in 0 initial
        hierarchy "a_initial"
    obelisk_sim.code_unit.decl 9700003 in 0 always
        hierarchy "z_always"
    obelisk_sim.code_unit.decl 9700004 in 0 port_initialize
        hierarchy "z_port_initialize" {internal}
    obelisk_sim.code_unit.decl 9700005 in 0 port_input
        hierarchy "z_port_input" {internal}
    obelisk_sim.scope.decl 0

    // SUPERSTEP: obelisk_sim.static_superstep = #obelisk_sim.static_superstep<version = 1
    // SUPERSTEP-SAME: actors = [@root, @z_always, @a_initial, @z_port_initialize, @z_port_input]
    // CHECK: compute_graph = #obelisk_sim.graph<
    // CHECK-SAME: nodes = [
    // CHECK-SAME: #obelisk_sim.fragment<id = [[INITIAL:[0-9]+]], function = @a_initial
    // CHECK-SAME: #obelisk_sim.fragment<id = [[ROOT:[0-9]+]], function = @root
    // CHECK-SAME: #obelisk_sim.fragment<id = [[ALWAYS:[0-9]+]], function = @z_always
    // CHECK-SAME: #obelisk_sim.fragment<id = [[PORT_INITIALIZE:[0-9]+]], function = @z_port_initialize
    // CHECK-SAME: #obelisk_sim.fragment<id = [[PORT_INPUT:[0-9]+]], function = @z_port_input
    // CHECK-SAME: kind = spawn
    // CHECK-SAME: #obelisk_sim.edge<source = [[ALWAYS]], target = [[INITIAL]], kind = process_order>
    // CHECK-SAME: #obelisk_sim.edge<source = [[PORT_INITIALIZE]], target = [[INITIAL]], kind = process_order>
    // CHECK-SAME: #obelisk_sim.edge<source = [[PORT_INPUT]], target = [[INITIAL]], kind = process_order>
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9700001 : i64} {
      %always = obelisk_sim.spawn @z_always(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %initial = obelisk_sim.spawn @a_initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %port_initialize = obelisk_sim.spawn @z_port_initialize(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %port_input = obelisk_sim.spawn @z_port_input(%ctx) :
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

    obelisk_sim.func @z_port_initialize(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 11 : i32, code_unit_id = 9700004 : i64,
                    internal} {
      obelisk_sim.return
    }

    obelisk_sim.func @z_port_input(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 9 : i32, code_unit_id = 9700005 : i64,
                    internal} {
      obelisk_sim.return
    }
  }
}
