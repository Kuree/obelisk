// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes='verify,coro-early,sroa,instcombine,simplifycfg,coro-split<reuse-storage>,coro-elide,coro-cleanup,sroa,instcombine,simplifycfg,dce,strip-dead-prototypes,verify' | FileCheck %s --check-prefix=SPLIT

// Creating a frame must not run source code or publish a semantic wait. The
// wrapper resumes it immediately within the same activation (LRM 4.3/4.6).
// The source side effect belongs only to the resume function after splitting.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @initial_suspend {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "initial_suspend"
    simulation.func @initial_suspend(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.dump.flush %ctx : (!simulation.context) -> ()
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @initial_suspend.__obelisk_native_execute
// CHECK: llvm.call @initial_suspend.__obelisk_coro_ramp
// CHECK-NEXT: llvm.br ^[[RESUME:bb[0-9]+]]
// CHECK: ^[[RESUME]]:
// CHECK: %[[HANDLE:[0-9]+]] = llvm.load
// CHECK-NEXT: llvm.intr.coro.resume %[[HANDLE]]
// CHECK-LABEL: llvm.func @initial_suspend.__obelisk_native_destroy

// SPLIT-LABEL: define void @initial_suspend.__obelisk_coro_ramp(
// SPLIT-NOT: call i32 @obelisk_rt_v1_dump_flush
// SPLIT-LABEL: define i32 @initial_suspend.__obelisk_native_requirements(
// SPLIT-LABEL: define internal fastcc void @initial_suspend.__obelisk_coro_ramp.resume(
// SPLIT: call i32 @obelisk_rt_v1_dump_flush
// SPLIT-LABEL: define internal fastcc void @initial_suspend.__obelisk_coro_ramp.destroy(
// SPLIT-NOT: call i32 @obelisk_rt_v1_dump_flush
