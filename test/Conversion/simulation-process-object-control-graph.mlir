// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  // CHECK-LABEL: obelisk_sim.design @process_control_graph attributes {
  // CHECK-SAME: compute_graph = #obelisk_sim.graph<
  // CHECK-SAME: action = process_control
  // CHECK-SAME: #obelisk_sim.edge<{{.*}}kind = resume>
  obelisk_sim.design @process_control_graph {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.control"

    obelisk_sim.func @control(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %current = obelisk_sim.process.current
      // CHECK: obelisk_sim.process.control suspend %{{.*}} to ^{{.*}} {site = #obelisk_sim.continuation<id = [[SITE:[1-9][0-9]*]]>}
      obelisk_sim.process.control suspend %current to ^continued
    ^continued:
      obelisk_sim.return
    }
  }
}
