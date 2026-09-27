// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3},symbol-dce))' | FileCheck %s

module {
  simulation.design @empty_task {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 task hierarchy "empty"
    simulation.code_unit.decl 2 in 0 always hierarchy "actor"

    simulation.func private @empty(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 12 : i32} {
      simulation.return
    }

    simulation.func @actor(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 3 : i32} {
      cf.br ^call
    ^call:
      simulation.task.call @empty(%ctx) arguments 1 to ^done
          : !simulation.context
    ^done:
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @actor
// CHECK: cf.br ^bb1
// CHECK-NOT: simulation.task.call
// CHECK-NOT: simulation.func private @empty
