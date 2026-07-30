// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @control {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "control.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "control.child"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %child = obelisk_sim.spawn @child(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %activation = obelisk_sim.control.enter 7
      obelisk_sim.control.leave %activation
      obelisk_sim.control.disable 7 activation %activation
      %static = obelisk_sim.static.once 11
      %deferred = obelisk_sim.assert.deferred_once 13
      obelisk_sim.monitor.register %child
      obelisk_sim.monitor.control true
      %current = obelisk_sim.monitor.current
      obelisk_sim.children.disable
      obelisk_sim.return
    }

    obelisk_sim.func @child(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}

// CHECK-DAG: llvm.func @obelisk_rt_v1_control_enter
// CHECK-DAG: llvm.func @obelisk_rt_v1_control_leave
// CHECK-DAG: llvm.func @obelisk_rt_v1_control_disable
// CHECK-DAG: llvm.func @obelisk_rt_v1_static_once
// CHECK-DAG: llvm.func @obelisk_rt_v1_deferred_once
// CHECK-DAG: llvm.func @obelisk_rt_v1_monitor_register
// CHECK-DAG: llvm.func @obelisk_rt_v1_monitor_control
// CHECK-DAG: llvm.func @obelisk_rt_v1_monitor_current
// CHECK-DAG: llvm.func @obelisk_rt_v1_scheduler_disable_children
// CHECK: llvm.func @root
// CHECK: llvm.call @obelisk_rt_v1_control_enter
// CHECK: llvm.call @obelisk_rt_v1_control_leave
// CHECK: llvm.call @obelisk_rt_v1_control_disable
// CHECK: llvm.call @obelisk_rt_v1_static_once
// CHECK: llvm.call @obelisk_rt_v1_deferred_once
// CHECK: llvm.call @obelisk_rt_v1_monitor_register
// CHECK: llvm.call @obelisk_rt_v1_monitor_control
// CHECK: llvm.call @obelisk_rt_v1_monitor_current
// CHECK: llvm.call @obelisk_rt_v1_scheduler_disable_children
// CHECK-NOT: obelisk_sim.control
// CHECK-NOT: obelisk_sim.static.once
// CHECK-NOT: obelisk_sim.assert.deferred_once
// CHECK-NOT: obelisk_sim.monitor
// CHECK-NOT: obelisk_sim.children.disable
