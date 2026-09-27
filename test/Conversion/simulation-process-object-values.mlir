// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @process_object_values {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.parent"
    simulation.code_unit.decl 3 in 0 initial hierarchy "top.child"
    simulation.code_unit.decl 4 in 0 task hierarchy "top.callee"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %parent = simulation.spawn @parent(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @parent(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %child = simulation.spawn @child(%ctx) :
          !simulation.context -> !simulation.process
      %null = simulation.process.null
      %current = simulation.process.current
      %is_null = simulation.process.equal %current, %null
      simulation.suspend.await %child to ^after_await
    ^after_await:
      simulation.task.call @callee(%ctx, %current) arguments 1 to ^done :
          !simulation.context, !simulation.process
    ^done(%continued: !simulation.process):
      %status = simulation.process.status %continued
      simulation.return
    }

    simulation.func @child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      simulation.return
    }

    simulation.func private @callee(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 4 : i64} {
      simulation.return
    }
  }
}

// NATIVE-DAG: llvm.func @obelisk_rt_v1_process_current(!llvm.ptr) -> i64
// NATIVE-DAG: llvm.func @obelisk_rt_v1_process_status(!llvm.ptr, i64, !llvm.ptr) -> i32
// NATIVE-LABEL: llvm.func @parent
// NATIVE: llvm.call @child.__obelisk_spawn
// NATIVE: llvm.mlir.constant(0 : i64)
// NATIVE: llvm.call @obelisk_rt_v1_process_current
// NATIVE: llvm.icmp "eq"
// NATIVE: llvm.call @obelisk_rt_v1_process_status
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_fail
// NATIVE-LABEL: llvm.func @child.__obelisk_spawn
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_process_token
// NATIVE: llvm.mlir.constant(-9223372036854775808 : i64)
// NATIVE: llvm.or

// The process value is one two-state 64-bit logical token in bytecode, rather
// than the historical 32-byte generic descriptor handle. The serialized
// records pin child spawn, null/current/equality, the await token, a generic
// process continuation StoreFrame/LoadFrame pair, and status. Frame transfers
// have flags 0 and size 8; descriptor-process transfers used flags 6.
// BYTECODE: obelisk.bytecode.image = array<i8:
// Spawn child (intrinsic site 1), null token, current (site 2), and equality.
// BYTECODE-SAME: 36, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
// BYTECODE-SAME: 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0
// BYTECODE-SAME: 36, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0
// BYTECODE-SAME: 17, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0
// Await stores the spawned process token directly into stable_id at offset 40.
// BYTECODE-SAME: 24, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0
// The task continuation stores current at frame offset 0, then calls function
// 3 with continuation 2. Status is intrinsic site 3 after resumption.
// BYTECODE-SAME: 24, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
// BYTECODE-SAME: 40, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0
// BYTECODE-SAME: 36, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0
// BYTECODE-SAME: 23, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
// Intrinsic signatures: process.current 0x1022c and process.status 0x1022d.
// BYTECODE-SAME: 44, 2, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
// BYTECODE-SAME: 45, 2, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
// Sites map current output to register 3, and status register 3 to output 5.
// BYTECODE-SAME: 2, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0
// BYTECODE-SAME: 3, 0, 0, 0, 6, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0
// BYTECODE: obelisk.bytecode.function = 1 : i32
// BYTECODE: obelisk.bytecode.scratch_size = 136 : i64
// BYTECODE: obelisk.bytecode.function = 2 : i32
