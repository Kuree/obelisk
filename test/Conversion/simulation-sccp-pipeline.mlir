// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(symbol-dce,obelisk-sim-sccp,obelisk_sim.func(canonicalize,cse)))' | FileCheck %s --check-prefix=BEFORE-DCE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(symbol-dce,obelisk-sim-sccp,obelisk_sim.func(canonicalize,cse),symbol-dce,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=FINAL --implicit-check-not=@dead_after_sccp
// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(symbol-dce,obelisk-sim-sccp,obelisk_sim.func(canonicalize,cse),obelisk-sim-build-compute-graph,symbol-dce,obelisk-sim-verify-compute-graph))' 2>&1 | FileCheck %s --check-prefix=STALE

module {
  obelisk_sim.design @pipeline {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.pipeline.dead_after_sccp.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %condition = arith.constant true
      cf.cond_br %condition, ^live, ^dead
    ^live:
      obelisk_sim.return
    ^dead:
      %process = obelisk_sim.spawn @dead_after_sccp(%ctx, %storage) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    // The first SymbolDCE must retain this process because the input still has
    // a spawn edge. SCCP and canonicalization erase that edge, making the second
    // SymbolDCE necessary before graph construction.
    // BEFORE-DCE: obelisk_sim.func private @dead_after_sccp
    obelisk_sim.func private @dead_after_sccp(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %storage: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %value = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %storage : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }
}

// FINAL: compute_graph = #obelisk_sim.graph
// FINAL: obelisk_sim.func @__obelisk_root

// A post-graph SymbolDCE removes the graph-only process symbol and leaves its
// fragment reference stale, which the graph verifier must reject.
// STALE: error: {{.*}}compute graph does not match the executable CFG
// STALE: function = @dead_after_sccp
