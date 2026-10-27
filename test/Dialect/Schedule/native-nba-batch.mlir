// RUN: talos-opt %s --cse | FileCheck %s
// RUN: talos-opt %s --convert-native-schedule-actions-to-llvm | FileCheck %s --check-prefix=LOWER

// A commit region is effectful even when its status is unused. Two barriers
// remain separate; conversion packs each region's admitted roots into a table.
// LOWER-DAG: llvm.mlir.global internal constant @__talos_nba_batch_roots{{.*}}(dense<[1, 65]>
// LOWER-DAG: llvm.mlir.global internal constant @__talos_nba_batch_roots{{.*}}(dense<2>
module {
  llvm.func @barriers(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    // CHECK: schedule.nba.batch
    // CHECK: schedule.nba.commit 1
    // CHECK: schedule.nba.commit 65
    // CHECK: schedule.nba.yield
    // LOWER: llvm.call @talos_rt_v1_static_nba_commit_batch
    %first = schedule.nba.batch %ctx, %region, %changed {
      schedule.nba.commit 1
      schedule.nba.commit 65
      schedule.nba.yield
    } : (!llvm.ptr, i32, !llvm.ptr) -> i32
    // CHECK: schedule.nba.batch
    // LOWER: llvm.call @talos_rt_v1_static_nba_commit_batch
    %second = schedule.nba.batch %ctx, %region, %changed {
      schedule.nba.commit 2
      schedule.nba.yield
    } : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
