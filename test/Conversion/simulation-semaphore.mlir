// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @semaphore {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.worker"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %worker = obelisk_sim.spawn @worker(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @worker(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %semaphore = obelisk_sim.semaphore.create %zero :
          (i32) -> !obelisk_sim.semaphore
      obelisk_sim.semaphore.put %semaphore, %one :
          (!obelisk_sim.semaphore, i32) -> ()
      cf.br ^attempt(%semaphore : !obelisk_sim.semaphore)
    ^attempt(%candidate: !obelisk_sim.semaphore):
      %success = obelisk_sim.semaphore.try_get %candidate, %one :
          (!obelisk_sim.semaphore, i32) -> i1
      cf.cond_br %success, ^done, ^wait(%candidate : !obelisk_sim.semaphore)
    ^wait(%waiting: !obelisk_sim.semaphore):
      obelisk_sim.suspend.semaphore %one from %waiting to ^done :
          !obelisk_sim.semaphore
    ^done:
      obelisk_sim.return
    }
  }
}

// NATIVE-DAG: llvm.func @obelisk_rt_v1_semaphore_create
// NATIVE-DAG: llvm.func @obelisk_rt_v1_semaphore_put
// NATIVE-DAG: llvm.func @obelisk_rt_v1_semaphore_try_get
// NATIVE-LABEL: llvm.func @worker
// NATIVE: llvm.call @obelisk_rt_v1_semaphore_create
// NATIVE: llvm.call @obelisk_rt_v1_semaphore_put
// NATIVE: llvm.call @obelisk_rt_v1_semaphore_try_get
// NATIVE: llvm.store {{.*}} : i32, !llvm.ptr

// BYTECODE: obelisk.bytecode.image = array<i8:
