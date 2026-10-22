// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines --cse | FileCheck %s

// IEEE 1800-2017 6.7.1 and 28.12 resolve every net bit on its own, so bits
// with the same drivers, strengths and net kind share one resolution function.
// Native lowering evaluates it once per run of such bits on word-sized plane
// slices. Bits 0-63 have only the full-width driver: the net copies its planes.
// Bits 64-127 also have the upper driver: one bitwise function of both
// drivers' planes resolves the whole run, published as one transition.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @runs {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "runs.drive"
    simulation.net.decl 0 in 0 : !simulation.logic<128> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<128> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<128> design
        {driven_low = 64 : i64, driven_width = 64 : i64}
    simulation.func @drive_full(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<128> {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<128>>
      simulation.driver.drive %driver = %value :
          !simulation.driver<!simulation.logic<128>>,
          !simulation.logic<128>
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @drive_full
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// The low run is this drive's own slice of the stored value.
// CHECK: %[[LOWV:.*]] = llvm.trunc %arg1 : i128 to i64
// CHECK-NEXT: %[[LOWU:.*]] = llvm.trunc %arg2 : i128 to i64
// Two strong wire drivers: the value plane is 1 exactly when both offer 1 or
// z (a value-plane 1), one i64 AND for the whole run.
// CHECK: %[[OWNV:.*]] = llvm.trunc %{{.*}} : i128 to i64
// CHECK: %[[OTHERV:.*]] = llvm.load %{{.*}} : !llvm.ptr -> i64
// CHECK: %[[HIGHV:.*]] = llvm.and %[[OTHERV]], %[[OWNV]] : i64
// CHECK: %[[LOWW:.*]] = llvm.mlir.constant(64 : i64) : i64
// CHECK-NEXT: llvm.call @obelisk_rt_v1_scheduler_static_transition(%{{.*}}, %{{.*}}, %{{.*}}, %[[LOWW]], %{{.*}}, %{{.*}}, %[[LOWV]], %[[LOWU]])
// CHECK: %[[HIGH:.*]] = llvm.mlir.constant(64 : i64) : i64
// CHECK-NEXT: llvm.mlir.constant(1 : i32) : i32
// CHECK-NEXT: llvm.call @obelisk_rt_v1_scheduler_static_transition(%{{.*}}, %{{.*}}, %[[HIGH]], %[[HIGH]], %{{.*}}, %{{.*}}, %[[HIGHV]], %{{.*}})
// CHECK-NOT: llvm.call @obelisk_rt_v1_scheduler_static_transition
// CHECK-NOT: llvm.call @obelisk_rt_v1_strength_resolve_kind
// CHECK: llvm.return
