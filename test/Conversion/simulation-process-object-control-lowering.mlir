// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: sed 's/native_scheduler = 0/native_scheduler = 2/' %s | not obelisk-opt - --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),convert-obelisk-sim-processes-to-llvm-coroutines)' 2>&1 | FileCheck %s --check-prefix=AOT-REJECT

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 0 : i32
} {
  obelisk_sim.design @process_object_control {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.control"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %process = obelisk_sim.spawn @control(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @control(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %current = obelisk_sim.process.current
      obelisk_sim.process.control resume %current to
          ^after_resume(%current : !obelisk_sim.process)
    ^after_resume(%resumed: !obelisk_sim.process):
      obelisk_sim.process.control suspend %resumed to
          ^after_suspend(%resumed : !obelisk_sim.process)
    ^after_suspend(%continued: !obelisk_sim.process):
      obelisk_sim.process.control kill %continued to ^unreachable
    ^unreachable:
      obelisk_sim.return
    }
  }
}

// Native lowering uses one shared runtime transaction for all three controls.
// CONTINUE branches directly through the continuation-frame reload shim in
// the same activation. Only explicit-suspend (4) or terminate (2) dispositions
// publish scheduler actions and coroutine-yield. A runtime failure reports
// scheduler status and terminates the activation.
// NATIVE-DAG: llvm.func @obelisk_rt_v1_process_control(!llvm.ptr, i64, i32, !llvm.ptr) -> i32
// NATIVE-LABEL: llvm.func @control.__obelisk_coro_ramp
// NATIVE-COUNT-3: llvm.call @obelisk_rt_v1_process_control
// NATIVE: ^[[RELOAD:bb[0-9]+]]:  // 3 preds:
// NATIVE: llvm.load {{.*}} : !llvm.ptr -> i64
// NATIVE: llvm.switch %{{.*}} : i32, ^{{.*}} [
// NATIVE-NEXT: 0: ^[[CONTINUE:bb[0-9]+]],
// NATIVE: ^[[CONTINUE]]:
// NATIVE-NEXT: llvm.br ^[[RELOAD]]
// NATIVE: llvm.mlir.constant(4 : i32)
// NATIVE: llvm.mlir.constant(2 : i32)
// NATIVE: llvm.intr.coro.suspend
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_fail

// Bytecode opcode 58 is append-only. Its flags are the proper ProcessControl
// enum encodings: resume=2, suspend=1, kill=0. Source0 is the initialized
// BITS64 shared process token; each immediate is a nonzero continuation ID.
// CONTINUE stays in the interpreter and jumps directly to the continuation PC;
// the explicit process continuation operands exercise frame store/reload.
// BYTECODE: obelisk.bytecode.image = array<i8:
// BYTECODE-SAME: 58, 0, 2, 0
// BYTECODE-SAME: 58, 0, 1, 0
// BYTECODE-SAME: 58, 0, 0, 0

// Generated AOT actor tables do not yet expose a transactional dynamic
// suspend/resume/kill protocol. Auto safely selects the generic scheduler;
// an explicit AOT request is rejected rather than leaving stale actor state.
// AOT-REJECT: process control requires generic ordering
