// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

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
      obelisk_sim.control.boundary %activation resume ^resume body ^body
    ^body:
      obelisk_sim.control.leave %activation
      cf.br ^resume
    ^resume:
      obelisk_sim.control.disable 7 activation %activation
      %escape = obelisk_sim.control.escape_pending
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
// NATIVE-NOT: obelisk_sim.control
// NATIVE-NOT: obelisk_sim.static.once
// NATIVE-NOT: obelisk_sim.assert.deferred_once
// NATIVE-NOT: obelisk_sim.monitor
// NATIVE-NOT: obelisk_sim.children.disable

// The append-only control-boundary intrinsic stores the nonzero resume
// continuation in flags and takes only the 64-bit dynamic activation token.
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010239 inputs=1 outputs=0 flags={{[1-9][0-9]*}}
// BYTECODE: intrinsic {{[0-9]+}}: id=0x0001023a inputs=0 outputs=1 flags=0
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010239 inputs={{\[[0-9]+\]}} outputs=[]
