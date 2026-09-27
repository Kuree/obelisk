// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @control {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "control.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "control.child"

    simulation.func @root(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %child = simulation.spawn @child(%ctx) :
          !simulation.context -> !simulation.process
      %activation = simulation.control.enter 7
      simulation.control.boundary %activation resume ^resume body ^body
    ^body:
      simulation.control.leave %activation
      cf.br ^resume
    ^resume:
      simulation.control.disable 7 activation %activation
      %escape = simulation.control.escape_pending
      %static = simulation.static.once 11
      %deferred = simulation.assert.deferred_once 13
      simulation.monitor.register %child
      simulation.monitor.control true
      %current = simulation.monitor.current
      simulation.children.disable
      simulation.return
    }

    simulation.func @child(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      simulation.return
    }
  }
}

// NATIVE-DAG: llvm.func @obelisk_rt_v1_control_enter
// NATIVE-DAG: llvm.func @obelisk_rt_v1_control_boundary
// NATIVE-DAG: llvm.func @obelisk_rt_v1_control_leave
// NATIVE-DAG: llvm.func @obelisk_rt_v1_control_disable
// NATIVE-DAG: llvm.func @obelisk_rt_v1_control_escape_pending
// NATIVE-DAG: llvm.func @obelisk_rt_v1_static_once
// NATIVE-DAG: llvm.func @obelisk_rt_v1_deferred_once
// NATIVE-DAG: llvm.func @obelisk_rt_v1_monitor_register_logical
// NATIVE-DAG: llvm.func @obelisk_rt_v1_monitor_control
// NATIVE-DAG: llvm.func @obelisk_rt_v1_monitor_current
// NATIVE-DAG: llvm.func @obelisk_rt_v1_scheduler_disable_children
// NATIVE: llvm.func @root
// NATIVE: llvm.call @obelisk_rt_v1_control_enter
// NATIVE: llvm.call @obelisk_rt_v1_control_boundary
// NATIVE: llvm.call @obelisk_rt_v1_control_leave
// NATIVE: llvm.call @obelisk_rt_v1_control_disable
// NATIVE: llvm.call @obelisk_rt_v1_control_escape_pending
// NATIVE: llvm.call @obelisk_rt_v1_static_once
// NATIVE: llvm.call @obelisk_rt_v1_deferred_once
// NATIVE: llvm.call @obelisk_rt_v1_monitor_register_logical
// NATIVE: llvm.call @obelisk_rt_v1_monitor_control
// NATIVE: llvm.call @obelisk_rt_v1_monitor_current
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_disable_children
// NATIVE-NOT: simulation.control
// NATIVE-NOT: simulation.static.once
// NATIVE-NOT: simulation.assert.deferred_once
// NATIVE-NOT: simulation.monitor
// NATIVE-NOT: simulation.children.disable

// The append-only control-boundary intrinsic stores the nonzero resume
// continuation in flags and takes only the 64-bit dynamic activation token.
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010239 inputs=1 outputs=0 flags={{[1-9][0-9]*}}
// BYTECODE: intrinsic {{[0-9]+}}: id=0x0001023a inputs=0 outputs=1 flags=0
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010239 inputs={{\[[0-9]+\]}} outputs=[]
