// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=0}))' 2>&1 | FileCheck %s

module {
  simulation.design @recursive_process_control {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.recursive"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.actor"

    simulation.func private @recursive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      simulation.call @recursive(%ctx, %process) :
          (!simulation.context, !simulation.process) -> ()
      simulation.process.control suspend %process to ^continued
    ^continued:
      simulation.return
    }

    simulation.func @actor(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %current = simulation.process.current
      simulation.call @recursive(%ctx, %current) :
          (!simulation.context, !simulation.process) -> ()
      simulation.return
    }
  }
}

// CHECK: 'simulation.call' op cannot safely propagate process control through this zero-time call
// CHECK-SAME: call is in a recursive SCC
