// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @events {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "events.process"

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %event = obelisk_sim.context.event %ctx[7] : !obelisk_sim.event
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.event.trigger %event nonblocking = false
      obelisk_sim.event.trigger %event after %delay nonblocking = true
      %triggered = obelisk_sim.event.triggered %event
      %equal = obelisk_sim.event.equal %event, %event
      obelisk_sim.return
    }
  }
}

// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_event_after
// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_event_triggered
// CHECK-LABEL: llvm.func @process
// CHECK: llvm.call @obelisk_rt_v1_scheduler_event_after
// CHECK: llvm.call @obelisk_rt_v1_scheduler_event_after
// CHECK: llvm.call @obelisk_rt_v1_scheduler_event_triggered
// CHECK: llvm.icmp "eq"
// CHECK-NOT: obelisk_sim.event
