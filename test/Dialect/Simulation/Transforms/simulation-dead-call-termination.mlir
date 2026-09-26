// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries))' | FileCheck %s

// IEEE 1800-2023 12.7.6 and 13.4: no memory effects does not imply that a
// function returns. Preserve an unbounded CFG and its transitive callers.
module {
  obelisk_sim.design @termination {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "spin"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "wrapper"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "caller"
    // CHECK-LABEL: obelisk_sim.func private @spin
    // CHECK: cf.br ^[[LOOP:.*]]
    // CHECK: ^[[LOOP]]:
    // CHECK: cf.br ^[[LOOP]]
    obelisk_sim.func private @spin(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      cf.br ^loop
    ^loop:
      cf.br ^loop
    }
    // CHECK-LABEL: obelisk_sim.func private @wrapper
    // CHECK: obelisk_sim.call @spin
    obelisk_sim.func private @wrapper(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      obelisk_sim.call @spin(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func @caller
    // CHECK: obelisk_sim.call @wrapper
    obelisk_sim.func @caller(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.call @wrapper(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}
