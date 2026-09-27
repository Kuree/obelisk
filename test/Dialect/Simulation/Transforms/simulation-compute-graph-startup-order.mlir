// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-plan-static-superstep))' | FileCheck %s --check-prefix=SUPERSTEP
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{workers=2},obelisk-sim-verify-compute-graph,obelisk-sim-plan-static-superstep{missed-remarks=true}))' > %t.workers 2> %t.workers-remarks
// RUN: FileCheck %s --check-prefix=NO-SUPERSTEP < %t.workers
// RUN: FileCheck %s --check-prefix=WORKER-REMARK < %t.workers-remarks

// Function names deliberately put the initial process before the always and
// internal port processes lexically. The graph assigns startup infrastructure
// first so it can establish time-zero sensitivities and values before an
// initial process runs.

module {
  // NO-SUPERSTEP-NOT: schedule.static_superstep
  // WORKER-REMARK: remark: static superstep not planned: static supersteps require one worker
  simulation.design @startup_order {
    simulation.code_unit.decl 9700001 in 0 root_initializer
        hierarchy "root"
    simulation.code_unit.decl 9700002 in 0 initial
        hierarchy "a_initial"
    simulation.code_unit.decl 9700003 in 0 always
        hierarchy "z_always"
    simulation.code_unit.decl 9700004 in 0 port_initialize
        hierarchy "z_port_initialize" {internal}
    simulation.code_unit.decl 9700005 in 0 port_input
        hierarchy "z_port_input" {internal}
    simulation.scope.decl 0

    // SUPERSTEP: schedule.static_superstep = #schedule.static_superstep<version = 1
    // SUPERSTEP-SAME: actors = [@root, @z_always, @a_initial, @z_port_initialize, @z_port_input]
    // CHECK: compute_graph = #schedule.graph<
    // CHECK-SAME: nodes = [
    // CHECK-SAME: #schedule.fragment<id = [[INITIAL:[0-9]+]], function = @a_initial
    // CHECK-SAME: #schedule.fragment<id = [[ROOT:[0-9]+]], function = @root
    // CHECK-SAME: #schedule.fragment<id = [[ALWAYS:[0-9]+]], function = @z_always
    // CHECK-SAME: #schedule.fragment<id = [[PORT_INITIALIZE:[0-9]+]], function = @z_port_initialize
    // CHECK-SAME: #schedule.fragment<id = [[PORT_INPUT:[0-9]+]], function = @z_port_input
    // CHECK-SAME: kind = spawn
    // CHECK-SAME: #schedule.edge<source = [[ALWAYS]], target = [[INITIAL]], kind = process_order>
    // CHECK-SAME: #schedule.edge<source = [[PORT_INITIALIZE]], target = [[INITIAL]], kind = process_order>
    // CHECK-SAME: #schedule.edge<source = [[PORT_INPUT]], target = [[INITIAL]], kind = process_order>
    simulation.func @root(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9700001 : i64} {
      %always = simulation.spawn @z_always(%ctx) :
          !simulation.context -> !simulation.process
      %initial = simulation.spawn @a_initial(%ctx) :
          !simulation.context -> !simulation.process
      %port_initialize = simulation.spawn @z_port_initialize(%ctx) :
          !simulation.context -> !simulation.process
      %port_input = simulation.spawn @z_port_input(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @a_initial(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9700002 : i64} {
      simulation.return
    }

    simulation.func @z_always(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 9700003 : i64} {
      simulation.return
    }

    simulation.func @z_port_initialize(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 11 : i32, code_unit_id = 9700004 : i64,
                    internal} {
      simulation.return
    }

    simulation.func @z_port_input(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 9 : i32, code_unit_id = 9700005 : i64,
                    internal} {
      simulation.return
    }
  }
}
