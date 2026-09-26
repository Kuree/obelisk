// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=0}))' 2>&1 | FileCheck %s

module {
  obelisk_sim.design @recursive_process_control {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.recursive"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.actor"

    obelisk_sim.func private @recursive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %process: !obelisk_sim.process {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.call @recursive(%ctx, %process) :
          (!obelisk_sim.context, !obelisk_sim.process) -> ()
      obelisk_sim.process.control suspend %process to ^continued
    ^continued:
      obelisk_sim.return
    }

    obelisk_sim.func @actor(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %current = obelisk_sim.process.current
      obelisk_sim.call @recursive(%ctx, %current) :
          (!obelisk_sim.context, !obelisk_sim.process) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK: 'obelisk_sim.call' op cannot safely propagate process control through this zero-time call
// CHECK-SAME: call is in a recursive SCC
