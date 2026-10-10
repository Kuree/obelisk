// RUN: obelisk-opt %s --split-input-file --finalize-native-llvm | FileCheck %s
// RUN: obelisk-opt %s --split-input-file --finalize-native-llvm --mlir-disable-threading | FileCheck %s

// A generated private symbol must not collide with an existing LLVM symbol.
// CHECK-LABEL: llvm.func @work.__obelisk_pending()
// CHECK: llvm.func internal @[[CORE:work.__obelisk_pending_[0-9]+]](
// CHECK-LABEL: llvm.func @work(%{{.*}}: i64)
// CHECK: llvm.call @[[CORE]]
// CHECK: llvm.func internal @caller.__obelisk_pending(%{{.*}}: i64, %{{.*}}: !llvm.ptr {llvm.noalias, llvm.nocapture})
// CHECK: llvm.call @[[CORE]](%{{.*}}, %{{.*}}) {schedule.eval.direct_call} : (i64, !llvm.ptr) -> ()
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", schedule.eval.runtime_calendar, schedule.eval.transaction_roots = [{root = array<i64: 1, 0, 1, 0, 32, 4294967295, 2, 0>, accumulator = @acc, sites = array<i64: 1>}]} {
  llvm.mlir.global internal @acc(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global internal @__obelisk_aot_nba_dirty_roots_v1(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @__obelisk_aot_nba_dirty_summary_v1(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @__obelisk_current_context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.func @work.__obelisk_pending()
  llvm.func @work(%arg: i64) attributes {schedule.eval.call_closure_root} {
    %acc = llvm.mlir.addressof @acc : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %one = llvm.mlir.constant(1 : i32) : i32
    %zero = llvm.mlir.constant(0 : i32) : i32
    %bits = llvm.mlir.constant(4294967295 : i64) : i64
    llvm.store %bits, %acc : i64, !llvm.ptr
    llvm.store %bits, %mask : i64, !llvm.ptr
    llvm.store %one, %valid : i32, !llvm.ptr
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %marked = llvm.mlir.constant(1 : i64) : i64
    llvm.store %marked, %dirty {schedule.eval.pending_dirty_root = 0 : i32, schedule.eval.pending_stage_spec = array<i64: 0, 1, 32, 4294967295, 2, 1>} : i64, !llvm.ptr
    %present = llvm.load %valid {schedule.eval.pending_dirty_root = 0 : i32} : !llvm.ptr -> i32
    llvm.store %zero, %valid : i32, !llvm.ptr
    llvm.return
  }
  llvm.func @caller(%arg: i64) attributes {schedule.eval.call_closure_root} {
    llvm.call @work(%arg) {schedule.eval.direct_call} : (i64) -> ()
    llvm.return
  }
}

// -----

// An escaped accumulator address can alias the capture through inttoptr.
// LRM 4.9.4/10.4.2 requires one coherent captured payload. Keep it canonical.
// CHECK-LABEL: module attributes
// CHECK-NOT: .__obelisk_pending
// CHECK: llvm.func @work()
// CHECK-NOT: .__obelisk_pending
// CHECK: llvm.return
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", schedule.eval.runtime_calendar, schedule.eval.transaction_roots = [{root = array<i64: 1, 0, 1, 0, 32, 4294967295, 2, 0>, accumulator = @acc, sites = array<i64: 1>}]} {
  llvm.mlir.global internal @acc(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global internal @__obelisk_aot_nba_dirty_roots_v1(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @__obelisk_aot_nba_dirty_summary_v1(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @__obelisk_current_context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.func @work() attributes {schedule.eval.call_closure_root} {
    %acc = llvm.mlir.addressof @acc : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %one = llvm.mlir.constant(1 : i32) : i32
    %zero = llvm.mlir.constant(0 : i32) : i32
    %bits = llvm.mlir.constant(4294967295 : i64) : i64
    llvm.store %bits, %acc : i64, !llvm.ptr
    llvm.store %bits, %mask : i64, !llvm.ptr
    llvm.store %one, %valid : i32, !llvm.ptr
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %marked = llvm.mlir.constant(1 : i64) : i64
    llvm.store %marked, %dirty {schedule.eval.pending_dirty_root = 0 : i32, schedule.eval.pending_stage_spec = array<i64: 0, 1, 32, 4294967295, 2, 1>} : i64, !llvm.ptr
    %escaped = llvm.ptrtoint %acc : !llvm.ptr to i64
    %alias = llvm.inttoptr %escaped : i64 to !llvm.ptr
    llvm.store %bits, %alias : i64, !llvm.ptr
    %present = llvm.load %valid {schedule.eval.pending_dirty_root = 0 : i32} : !llvm.ptr -> i32
    llvm.store %zero, %valid : i32, !llvm.ptr
    llvm.return
  }
}

// -----

// A tail-call contract cannot carry an appended caller-owned frame. The ABI
// preflight must keep the whole transaction canonical before moving any body.
// CHECK-LABEL: module attributes
// CHECK-NOT: .__obelisk_pending
// CHECK: llvm.func @work(
// CHECK-NOT: .__obelisk_pending
// CHECK: llvm.call tail @work(
// CHECK-NOT: .__obelisk_pending
// CHECK: llvm.return
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", schedule.eval.runtime_calendar, schedule.eval.transaction_roots = [{root = array<i64: 1, 0, 1, 0, 32, 4294967295, 2, 0>, accumulator = @acc, sites = array<i64: 1>}]} {
  llvm.mlir.global internal @acc(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global internal @__obelisk_aot_nba_dirty_roots_v1(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @__obelisk_aot_nba_dirty_summary_v1(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @__obelisk_current_context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.func @work(%arg: i64) attributes {schedule.eval.call_closure_root} {
    %acc = llvm.mlir.addressof @acc : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %one = llvm.mlir.constant(1 : i32) : i32
    %zero = llvm.mlir.constant(0 : i32) : i32
    %bits = llvm.mlir.constant(4294967295 : i64) : i64
    llvm.store %bits, %acc : i64, !llvm.ptr
    llvm.store %bits, %mask : i64, !llvm.ptr
    llvm.store %one, %valid : i32, !llvm.ptr
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %marked = llvm.mlir.constant(1 : i64) : i64
    llvm.store %marked, %dirty {schedule.eval.pending_dirty_root = 0 : i32, schedule.eval.pending_stage_spec = array<i64: 0, 1, 32, 4294967295, 2, 1>} : i64, !llvm.ptr
    %present = llvm.load %valid {schedule.eval.pending_dirty_root = 0 : i32} : !llvm.ptr -> i32
    llvm.store %zero, %valid : i32, !llvm.ptr
    llvm.return
  }
  llvm.func @caller(%arg: i64) attributes {schedule.eval.call_closure_root} {
    llvm.call tail @work(%arg) {schedule.eval.direct_call} : (i64) -> ()
    llvm.return
  }
}

// -----

// A dynamically addressed index can observe any canonical leaf. Retain the
// canonical ownership interface when the sparse projection is not exact.
// CHECK-LABEL: module attributes
// CHECK-NOT: .__obelisk_pending
// CHECK: llvm.func @work(
// CHECK-NOT: .__obelisk_pending
// CHECK: llvm.getelementptr %{{.*}}[%{{.*}}]
// CHECK-NOT: .__obelisk_pending
// CHECK: llvm.return
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", schedule.eval.runtime_calendar, schedule.eval.transaction_roots = [{root = array<i64: 1, 0, 1, 0, 32, 4294967295, 2, 0>, accumulator = @acc, sites = array<i64: 1>}]} {
  llvm.mlir.global internal @acc(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global internal @__obelisk_aot_nba_dirty_roots_v1(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @__obelisk_aot_nba_dirty_summary_v1(dense<0> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @__obelisk_current_context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.func @work(%offset: i64) attributes {schedule.eval.call_closure_root} {
    %acc = llvm.mlir.addressof @acc : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %one = llvm.mlir.constant(1 : i32) : i32
    %zero = llvm.mlir.constant(0 : i32) : i32
    %bits = llvm.mlir.constant(4294967295 : i64) : i64
    llvm.store %bits, %acc : i64, !llvm.ptr
    llvm.store %bits, %mask : i64, !llvm.ptr
    llvm.store %one, %valid : i32, !llvm.ptr
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %marked = llvm.mlir.constant(1 : i64) : i64
    llvm.store %marked, %dirty {schedule.eval.pending_dirty_root = 0 : i32, schedule.eval.pending_stage_spec = array<i64: 0, 1, 32, 4294967295, 2, 1>} : i64, !llvm.ptr
    %alias = llvm.getelementptr %dirty[%offset] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %opaqueIndex = llvm.load %alias : !llvm.ptr -> i64
    %present = llvm.load %valid {schedule.eval.pending_dirty_root = 0 : i32} : !llvm.ptr -> i32
    llvm.store %zero, %valid : i32, !llvm.ptr
    llvm.return
  }
}
