// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3},symbol-dce))' | FileCheck %s

module {
  obelisk_sim.design @empty_task {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 task hierarchy "empty"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "actor"

    obelisk_sim.func private @empty(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 12 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @actor(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 3 : i32} {
      cf.br ^call
    ^call:
      obelisk_sim.task.call @empty(%ctx) arguments 1 to ^done
          : !obelisk_sim.context
    ^done:
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @actor
// CHECK: cf.br ^bb1
// CHECK-NOT: obelisk_sim.task.call
// CHECK-NOT: obelisk_sim.func private @empty
