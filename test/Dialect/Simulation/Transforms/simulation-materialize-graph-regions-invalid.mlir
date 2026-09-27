// RUN: obelisk-opt %s --verify-diagnostics --pass-pipeline='builtin.module(simulation.design(obelisk-sim-materialize-graph-regions))'

module {
  // expected-error @below {{graph-region materialization requires a verified compute graph}}
  simulation.design @missing_graph {
    simulation.scope.decl 0
  }
}
