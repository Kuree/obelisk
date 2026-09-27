// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(symbol-dce,obelisk-sim-sccp,simulation.func(canonicalize,cse)))' | FileCheck %s --check-prefix=BEFORE-DCE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(symbol-dce,obelisk-sim-sccp,simulation.func(canonicalize,cse),symbol-dce,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=FINAL --implicit-check-not=@dead_after_sccp
// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(symbol-dce,obelisk-sim-sccp,simulation.func(canonicalize,cse),obelisk-sim-build-compute-graph,symbol-dce,obelisk-sim-verify-compute-graph))' 2>&1 | FileCheck %s --check-prefix=STALE

module {
  simulation.design @pipeline {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.pipeline.dead_after_sccp.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %condition = arith.constant true
      cf.cond_br %condition, ^live, ^dead
    ^live:
      simulation.return
    ^dead:
      %process = simulation.spawn @dead_after_sccp(%ctx, %storage) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    // The first SymbolDCE must retain this process because the input still has
    // a spawn edge. SCCP and canonicalization erase that edge, making the second
    // SymbolDCE necessary before graph construction.
    // BEFORE-DCE: simulation.func private @dead_after_sccp
    simulation.func private @dead_after_sccp(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %storage: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.ref.store %value to %storage : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}

// FINAL: compute_graph = #schedule.graph
// FINAL: simulation.func @__obelisk_root

// A post-graph SymbolDCE removes the graph-only process symbol and leaves its
// fragment reference stale, which the graph verifier must reject.
// STALE: error: {{.*}}compute graph does not match the executable CFG
// STALE: function = @dead_after_sccp
