// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.threaded
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.single
// RUN: diff %t.threaded %t.single
// RUN: FileCheck %s < %t.threaded

module {
  // Deliberately reverse symbol order in the IR. The module-level test pass
  // must serialize complete design reports in symbol order.
  obelisk_sim.design @zeta {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @worker(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %unknown = obelisk_sim.logic.constant 0 : i1, 1 : i1 : !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }

  obelisk_sim.design @alpha {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @worker(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %known = obelisk_sim.logic.constant 1 : i1, 0 : i1 : !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}

// CHECK:      state-domain @alpha
// CHECK-NEXT: func @worker
// CHECK-NEXT:   bb0.op0.result0: two-state (logic-constant)
// CHECK-NEXT: state-domain @zeta
// CHECK-NEXT: func @worker
// CHECK-NEXT:   bb0.op0.result0: may-four-state (unknown-constant)
