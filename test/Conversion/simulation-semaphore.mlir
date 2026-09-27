// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @semaphore {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.worker"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %worker = simulation.spawn @worker(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @worker(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %semaphore = simulation.semaphore.create %zero :
          (i32) -> !simulation.semaphore
      simulation.semaphore.put %semaphore, %one :
          (!simulation.semaphore, i32) -> ()
      cf.br ^attempt(%semaphore : !simulation.semaphore)
    ^attempt(%candidate: !simulation.semaphore):
      %success = simulation.semaphore.try_get %candidate, %one :
          (!simulation.semaphore, i32) -> i1
      cf.cond_br %success, ^done, ^wait(%candidate : !simulation.semaphore)
    ^wait(%waiting: !simulation.semaphore):
      simulation.suspend.semaphore %one from %waiting to ^done :
          !simulation.semaphore
    ^done:
      simulation.return
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
