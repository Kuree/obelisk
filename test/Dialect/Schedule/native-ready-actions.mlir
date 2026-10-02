// RUN: obelisk-opt %s --schedule-coalesce-native-ready -o %t
// RUN: obelisk-opt %t --cse | FileCheck %s
// RUN: obelisk-opt %t --convert-native-schedule-actions-to-llvm | FileCheck %s --check-prefix=LOWER
// Pending-mask union must respect consume, alias, observation and publication.
module {
  llvm.func @updates(%base: !llvm.ptr, %ctx: !llvm.ptr) -> i64 {
    %one = arith.constant 1 : i64
    %two = arith.constant 2 : i64
    %zero = arith.constant 0 : i64
    // CHECK: %[[REMOVE:.*]] = arith.constant 1 : i64
    // CHECK: %[[ADD:.*]] = arith.constant 2 : i64
    // CHECK: schedule.ready.commit %{{.*}}, %[[REMOVE]], %[[ADD]]
    schedule.ready.update %base, %one {capacity = 128 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    schedule.ready.update %base, %two {capacity = 128 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    schedule.ready.update %base, %one {capacity = 128 : i64, word = 0 : i64, consume = true} : (!llvm.ptr, i64) -> ()
    // CHECK: llvm.load
    schedule.ready.update %base, %two {capacity = 128 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    %observed = llvm.load %base : !llvm.ptr -> i64
    // CHECK: schedule.ready.update
    schedule.ready.update %base, %one {capacity = 128 : i64, word = 0 : i64, consume = false} : (!llvm.ptr, i64) -> ()
    // CHECK: schedule.transition
    // CHECK: schedule.transition
    // LOWER-COUNT-2: llvm.call @obelisk_rt_v1_scheduler_static_transition
    schedule.transition %ctx, %zero, %zero, %zero, %one, %zero {static_state = 1 : i64, width = 1 : i64, source_owner = #schedule.source_owner<codeUnit = 9 : i64, continuation = 0 : i64>} : (!llvm.ptr, i64, i64, i64, i64, i64) -> ()
    schedule.transition %ctx, %zero, %one, %zero, %zero, %zero {static_state = 1 : i64, width = 1 : i64, source_owner = #schedule.source_owner<codeUnit = 9 : i64, continuation = 0 : i64>} : (!llvm.ptr, i64, i64, i64, i64, i64) -> ()
    llvm.return %observed : i64
  }
}
