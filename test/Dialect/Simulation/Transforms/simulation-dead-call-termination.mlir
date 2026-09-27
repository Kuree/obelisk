// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries))' | FileCheck %s

// IEEE 1800-2023 12.7.6 and 13.4: no memory effects does not imply that a
// function returns. Preserve an unbounded CFG and its transitive callers.
module {
  simulation.design @termination {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "spin"
    simulation.code_unit.decl 2 in 0 function hierarchy "wrapper"
    simulation.code_unit.decl 3 in 0 initial hierarchy "caller"
    // CHECK-LABEL: simulation.func private @spin
    // CHECK: cf.br ^[[LOOP:.*]]
    // CHECK: ^[[LOOP]]:
    // CHECK: cf.br ^[[LOOP]]
    simulation.func private @spin(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      cf.br ^loop
    ^loop:
      cf.br ^loop
    }
    // CHECK-LABEL: simulation.func private @wrapper
    // CHECK: simulation.call @spin
    simulation.func private @wrapper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      simulation.call @spin(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
    // CHECK-LABEL: simulation.func @caller
    // CHECK: simulation.call @wrapper
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      simulation.call @wrapper(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
  }
}
