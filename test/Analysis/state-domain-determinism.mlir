// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.threaded
// RUN: obelisk-opt %s -o /dev/null --mlir-disable-threading --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2> %t.single
// RUN: diff %t.threaded %t.single
// RUN: FileCheck %s < %t.threaded

module {
  // Deliberately reverse symbol order in the IR. The module-level test pass
  // must serialize complete design reports in symbol order.
  simulation.design @zeta {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.zeta.worker.9000001"
    simulation.scope.decl 0
    simulation.func @worker(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %unknown = simulation.logic.constant 0 : i1, 1 : i1 : !simulation.logic<1>
      simulation.return
    }
  }

  simulation.design @alpha {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.alpha.worker.9000001"
    simulation.scope.decl 0
    simulation.func @worker(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %known = simulation.logic.constant 1 : i1, 0 : i1 : !simulation.logic<1>
      simulation.return
    }
  }
}

// CHECK:      state-domain @alpha
// CHECK-NEXT: func @worker
// CHECK-NEXT:   bb0.op0.result0: two-state (logic-constant)
// CHECK-NEXT: state-domain @zeta
// CHECK-NEXT: func @worker
// CHECK-NEXT:   bb0.op0.result0: may-four-state (unknown-constant)
