// RUN: obelisk-opt %s --verify-diagnostics --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions))'

module {
  // expected-error @below {{graph-region materialization requires a verified compute graph}}
  obelisk_sim.design @missing_graph {
    obelisk_sim.scope.decl 0
  }
}
