// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  // CHECK-LABEL: simulation.design @process_control_graph attributes {
  // CHECK-SAME: compute_graph = #schedule.graph<
  // CHECK-SAME: action = process_control
  // CHECK-SAME: #schedule.edge<{{.*}}kind = resume>
  simulation.design @process_control_graph {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.control"

    simulation.func @control(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %current = simulation.process.current
      // CHECK: simulation.process.control suspend %{{.*}} to ^{{.*}} {site = #schedule.continuation<id = [[SITE:[1-9][0-9]*]]>}
      simulation.process.control suspend %current to ^continued
    ^continued:
      simulation.return
    }
  }
}
