// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @process_random_state {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.parent"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "top.child"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %parent = obelisk_sim.spawn @parent(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @parent(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %child = obelisk_sim.spawn @child(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.suspend.await %child to ^after_child
    ^after_child:
      // Process identity and its two RNG words survive FINISHED as tombstone
      // metadata, so the same typed operations remain valid after await.
      %state, %increment = obelisk_sim.process.random_state %child
      obelisk_sim.process.set_random_state %child, %state, %increment
      obelisk_sim.return
    }

    obelisk_sim.func @child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.return
    }
  }
}

// NATIVE-DAG: llvm.func @obelisk_rt_v1_process_random_get(!llvm.ptr, i64, !llvm.ptr) -> i32
// NATIVE-DAG: llvm.func @obelisk_rt_v1_process_random_set(!llvm.ptr, i64, !llvm.ptr) -> i32
// NATIVE-LABEL: llvm.func @parent
// NATIVE: ^{{.*}}(%[[PROCESS:.*]]: i64):
// NATIVE: %[[GET_STATUS:.*]] = llvm.call @obelisk_rt_v1_process_random_get({{.*}}, %[[PROCESS]], {{.*}}) : (!llvm.ptr, i64, !llvm.ptr) -> i32
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_fail({{.*}}, %[[GET_STATUS]])
// NATIVE: %[[STATE:.*]] = llvm.load {{.*}} : !llvm.ptr -> i64
// NATIVE: %[[INCREMENT:.*]] = llvm.load {{.*}} : !llvm.ptr -> i64
// NATIVE: llvm.store %[[STATE]], {{.*}} : i64, !llvm.ptr
// NATIVE: llvm.store %[[INCREMENT]], {{.*}} : i64, !llvm.ptr
// NATIVE: %[[SET_STATUS:.*]] = llvm.call @obelisk_rt_v1_process_random_set({{.*}}, %[[PROCESS]], {{.*}}) : (!llvm.ptr, i64, !llvm.ptr) -> i32
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_fail({{.*}}, %[[SET_STATUS]])

// The append-only process RNG intrinsic IDs are 0x1022e and 0x1022f.
// Their serialized signatures pin the typed two-state ABI:
// get(process)->(state,increment) and set(process,state,increment).
// BYTECODE: obelisk.bytecode.image = array<i8:
// BYTECODE-SAME: 46, 2, 1, 0, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0
// BYTECODE-SAME: 47, 2, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
