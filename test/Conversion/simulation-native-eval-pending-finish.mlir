// RUN: talos-opt %s --finalize-native-llvm | FileCheck %s

// LRM 20.2, 4.9.4: a finishing NBA preparation in the eval closure goes
// through the private-index wrapper, which cancels every sparse bitmap word
// before the next private consumer or wrapper export.
// ../Runtime/simulation-native-eval-pending-finish.test executes this
// self-checking program.
// CHECK-LABEL: llvm.func internal @__talos_eval_dispatch_v1.__talos_pending(
// CHECK: llvm.call @talos_rt_v1_scheduler_prepare_eval_nba_local_write(
// CHECK-LABEL: llvm.func @__talos_eval_dispatch_v1()
// CHECK: llvm.call @__talos_eval_dispatch_v1.__talos_pending(

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", schedule.eval.runtime_calendar, schedule.eval.transaction_roots = [{root = array<i64: 1, 0, 1, 0, 32, 4294967295, 2, 1>, accumulator = @acc0, sites = array<i64: 1>}, {root = array<i64: 65, 64, 65, 0, 32, 4294967295, 2, 0>, accumulator = @acc64, sites = array<i64: 2>}, {root = array<i64: 4097, 4096, 4097, 0, 32, 4294967295, 2, 0>, accumulator = @acc4096, sites = array<i64: 3>}]} {
  llvm.mlir.global @acc0(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @acc64(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @acc4096(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @__talos_aot_nba_dirty_roots_v1(dense<0> : tensor<520xi8>) : !llvm.array<520 x i8>
  llvm.mlir.global @__talos_aot_nba_dirty_summary_v1(dense<0> : tensor<16xi8>) : !llvm.array<16 x i8>
  llvm.mlir.global internal @__talos_current_context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.func @__talos_eval_write_guard_0_v1()
  llvm.func @talos_rt_v1_scheduler_execute_aot_actor(!llvm.ptr, i32) -> i32
  llvm.func @talos_rt_v1_scheduler_prepare_eval_nba(!llvm.ptr, !llvm.ptr) -> i32

  llvm.func @capture0() {
    %acc = llvm.mlir.addressof @acc0 : !llvm.ptr
    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %region = llvm.getelementptr %acc[100] : (!llvm.ptr) -> !llvm.ptr, i8
    %transient = llvm.getelementptr %acc[104] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.mlir.constant(1 : i64) : i64
    %unknown = llvm.mlir.constant(1 : i64) : i64
    %bits = llvm.mlir.constant(4294967295 : i64) : i64
    %one = llvm.mlir.constant(1 : i32) : i32
    %nba = llvm.mlir.constant(2 : i32) : i32
    %zero = llvm.mlir.constant(0 : i64) : i64
    llvm.store %value, %acc : i64, !llvm.ptr
    llvm.store %unknown, %xz : i64, !llvm.ptr
    llvm.store %bits, %mask : i64, !llvm.ptr
    llvm.store %one, %valid : i32, !llvm.ptr
    llvm.store %nba, %region : i32, !llvm.ptr
    llvm.store %zero, %transient : i64, !llvm.ptr
    %dirty = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 0 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(1 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 0 : i32, schedule.eval.pending_stage_spec = array<i64: 0, 1, 32, 4294967295, 2, 1>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1 : !llvm.ptr
    %page = llvm.getelementptr %summary[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %prior = llvm.load %page {schedule.eval.pending_summary_root = 0 : i32} : !llvm.ptr -> i64
    %pagebit = llvm.mlir.constant(1 : i64) : i64
    %markedpage = llvm.or %prior, %pagebit : i64
    llvm.store %markedpage, %page {schedule.eval.pending_summary_root = 0 : i32} : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @consume0() -> i64 {
    %acc = llvm.mlir.addressof @acc0 : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.load %valid {schedule.eval.pending_dirty_root = 0 : i32} : !llvm.ptr -> i32
    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.load %acc : !llvm.ptr -> i64
    %unknown = llvm.load %xz : !llvm.ptr -> i64
    %scale = llvm.mlir.constant(128 : i64) : i64
    %zero = llvm.mlir.constant(0 : i32) : i32
    %zero64 = llvm.mlir.constant(0 : i64) : i64
    %weighted = llvm.mul %unknown, %scale : i64
    %sum = llvm.add %value, %weighted : i64
    %present = llvm.icmp "ne" %ready, %zero : i32
    %result = llvm.select %present, %sum, %zero64 : i1, i64
    llvm.store %zero, %valid : i32, !llvm.ptr
    llvm.return %result : i64
  }
  llvm.func @capture64() {
    %acc = llvm.mlir.addressof @acc64 : !llvm.ptr
    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %region = llvm.getelementptr %acc[100] : (!llvm.ptr) -> !llvm.ptr, i8
    %transient = llvm.getelementptr %acc[104] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.mlir.constant(2 : i64) : i64
    %unknown = llvm.mlir.constant(0 : i64) : i64
    %bits = llvm.mlir.constant(4294967295 : i64) : i64
    %one = llvm.mlir.constant(1 : i32) : i32
    %nba = llvm.mlir.constant(2 : i32) : i32
    %zero = llvm.mlir.constant(0 : i64) : i64
    llvm.store %value, %acc : i64, !llvm.ptr
    llvm.store %unknown, %xz : i64, !llvm.ptr
    llvm.store %bits, %mask : i64, !llvm.ptr
    llvm.store %one, %valid : i32, !llvm.ptr
    llvm.store %nba, %region : i32, !llvm.ptr
    llvm.store %zero, %transient : i64, !llvm.ptr
    %dirty = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 64 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(1 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 64 : i32, schedule.eval.pending_stage_spec = array<i64: 64, 65, 32, 4294967295, 2, 2>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1 : !llvm.ptr
    %page = llvm.getelementptr %summary[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %prior = llvm.load %page {schedule.eval.pending_summary_root = 64 : i32} : !llvm.ptr -> i64
    %pagebit = llvm.mlir.constant(2 : i64) : i64
    %markedpage = llvm.or %prior, %pagebit : i64
    llvm.store %markedpage, %page {schedule.eval.pending_summary_root = 64 : i32} : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @consume64() -> i64 {
    %acc = llvm.mlir.addressof @acc64 : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.load %valid {schedule.eval.pending_dirty_root = 64 : i32} : !llvm.ptr -> i32
    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.load %acc : !llvm.ptr -> i64
    %unknown = llvm.load %xz : !llvm.ptr -> i64
    %scale = llvm.mlir.constant(128 : i64) : i64
    %zero = llvm.mlir.constant(0 : i32) : i32
    %zero64 = llvm.mlir.constant(0 : i64) : i64
    %weighted = llvm.mul %unknown, %scale : i64
    %sum = llvm.add %value, %weighted : i64
    %present = llvm.icmp "ne" %ready, %zero : i32
    %result = llvm.select %present, %sum, %zero64 : i1, i64
    llvm.store %zero, %valid : i32, !llvm.ptr
    llvm.return %result : i64
  }
  llvm.func @capture4096() {
    %acc = llvm.mlir.addressof @acc4096 : !llvm.ptr
    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %region = llvm.getelementptr %acc[100] : (!llvm.ptr) -> !llvm.ptr, i8
    %transient = llvm.getelementptr %acc[104] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.mlir.constant(3 : i64) : i64
    %unknown = llvm.mlir.constant(0 : i64) : i64
    %bits = llvm.mlir.constant(4294967295 : i64) : i64
    %one = llvm.mlir.constant(1 : i32) : i32
    %nba = llvm.mlir.constant(2 : i32) : i32
    %zero = llvm.mlir.constant(0 : i64) : i64
    llvm.store %value, %acc : i64, !llvm.ptr
    llvm.store %unknown, %xz : i64, !llvm.ptr
    llvm.store %bits, %mask : i64, !llvm.ptr
    llvm.store %one, %valid : i32, !llvm.ptr
    llvm.store %nba, %region : i32, !llvm.ptr
    llvm.store %zero, %transient : i64, !llvm.ptr
    %dirty = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[512] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 4096 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(1 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 4096 : i32, schedule.eval.pending_stage_spec = array<i64: 4096, 4097, 32, 4294967295, 2, 3>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1 : !llvm.ptr
    %page = llvm.getelementptr %summary[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %prior = llvm.load %page {schedule.eval.pending_summary_root = 4096 : i32} : !llvm.ptr -> i64
    %pagebit = llvm.mlir.constant(1 : i64) : i64
    %markedpage = llvm.or %prior, %pagebit : i64
    llvm.store %markedpage, %page {schedule.eval.pending_summary_root = 4096 : i32} : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @consume4096() -> i64 {
    %acc = llvm.mlir.addressof @acc4096 : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.load %valid {schedule.eval.pending_dirty_root = 4096 : i32} : !llvm.ptr -> i32
    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.load %acc : !llvm.ptr -> i64
    %unknown = llvm.load %xz : !llvm.ptr -> i64
    %scale = llvm.mlir.constant(128 : i64) : i64
    %zero = llvm.mlir.constant(0 : i32) : i32
    %zero64 = llvm.mlir.constant(0 : i64) : i64
    %weighted = llvm.mul %unknown, %scale : i64
    %sum = llvm.add %value, %weighted : i64
    %present = llvm.icmp "ne" %ready, %zero : i32
    %result = llvm.select %present, %sum, %zero64 : i1, i64
    llvm.store %zero, %valid : i32, !llvm.ptr
    llvm.return %result : i64
  }
  llvm.func @canonical_only() -> i64 {
    %summary = llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1 : !llvm.ptr
    %bits = llvm.load %summary {schedule.eval.pending_runtime_root = 5000 : i32} : !llvm.ptr -> i64
    llvm.return %bits : i64
  }
  llvm.func @query() -> i64 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %base0 = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr0 = llvm.getelementptr %base0[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits0 = llvm.load %ptr0 : !llvm.ptr -> i64
    %sum0 = llvm.xor %zero, %bits0 : i64
    %base1 = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr1 = llvm.getelementptr %base1[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits1 = llvm.load %ptr1 : !llvm.ptr -> i64
    %sum1 = llvm.xor %sum0, %bits1 : i64
    %base2 = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr2 = llvm.getelementptr %base2[512] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits2 = llvm.load %ptr2 : !llvm.ptr -> i64
    %sum2 = llvm.xor %sum1, %bits2 : i64
    %base3 = llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1 : !llvm.ptr
    %ptr3 = llvm.getelementptr %base3[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits3 = llvm.load %ptr3 : !llvm.ptr -> i64
    %sum3 = llvm.xor %sum2, %bits3 : i64
    %base4 = llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1 : !llvm.ptr
    %ptr4 = llvm.getelementptr %base4[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits4 = llvm.load %ptr4 : !llvm.ptr -> i64
    %sum4 = llvm.xor %sum3, %bits4 : i64
    llvm.return %sum4 : i64
  }
  llvm.func @pending4096() -> i64 {
    %dirty = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[512] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits = llvm.load %leaf {schedule.eval.pending_dirty_root = 4096 : i32} : !llvm.ptr -> i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %result = llvm.and %bits, %one : i64
    llvm.return %result : i64
  }
  llvm.func @__talos_eval_dispatch_v1() -> i64 attributes {schedule.eval.call_closure_root} {
    %null = llvm.mlir.zero : !llvm.ptr
    %slot = llvm.mlir.constant(0 : i32) : i32
    %zero = llvm.mlir.constant(0 : i64) : i64
    llvm.call @capture0() : () -> ()
    llvm.call @capture64() : () -> ()
    llvm.call @capture4096() : () -> ()
    %canonical = llvm.call @canonical_only() : () -> i64
    %before = llvm.call @query() : () -> i64
    %expectedQuery = llvm.mlir.constant(3 : i64) : i64
    %queryOK = llvm.icmp "eq" %before, %expectedQuery : i64
    %last = llvm.call @pending4096() : () -> i64
    %one64 = llvm.mlir.constant(1 : i64) : i64
    %lastOK = llvm.icmp "eq" %last, %one64 : i64
    %one32 = llvm.mlir.constant(1 : i64) : i64
    %out = llvm.alloca %one32 x i32 : (i64) -> !llvm.ptr
    %status = llvm.call @talos_rt_v1_scheduler_prepare_eval_nba(%null, %out) : (!llvm.ptr, !llvm.ptr) -> i32
    %zero32 = llvm.mlir.constant(0 : i32) : i32
    %cutOK = llvm.icmp "eq" %status, %zero32 : i32
    %value0 = llvm.call @consume0() : () -> i64
    %sum0 = llvm.add %zero, %value0 : i64
    %value1 = llvm.call @consume64() : () -> i64
    %sum1 = llvm.add %sum0, %value1 : i64
    %value2 = llvm.call @consume4096() : () -> i64
    %sum2 = llvm.add %sum1, %value2 : i64
    %base0 = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr0 = llvm.getelementptr %base0[0] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr0 : i64, !llvm.ptr
    %base1 = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr1 = llvm.getelementptr %base1[8] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr1 : i64, !llvm.ptr
    %base2 = llvm.mlir.addressof @__talos_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr2 = llvm.getelementptr %base2[512] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr2 : i64, !llvm.ptr
    %base3 = llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1 : !llvm.ptr
    %ptr3 = llvm.getelementptr %base3[0] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr3 : i64, !llvm.ptr
    %base4 = llvm.mlir.addressof @__talos_aot_nba_dirty_summary_v1 : !llvm.ptr
    %ptr4 = llvm.getelementptr %base4[8] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr4 : i64, !llvm.ptr
    %after = llvm.call @query() : () -> i64
    %empty = llvm.icmp "eq" %after, %zero : i64
    %validCut = llvm.and %queryOK, %cutOK : i1
    %validLast = llvm.and %validCut, %lastOK : i1
    %valid = llvm.and %validLast, %empty : i1
    %failure = llvm.mlir.constant(-1 : i64) : i64
    %result = llvm.select %valid, %sum2, %failure : i1, i64
    llvm.return %result : i64
  }
  llvm.func @main() -> i32 {
    %first = llvm.call @__talos_eval_dispatch_v1() : () -> i64
    %second = llvm.call @__talos_eval_dispatch_v1() : () -> i64
    %expected = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i32) : i32
    %zero = llvm.mlir.constant(0 : i32) : i32
    %firstOK = llvm.icmp "eq" %first, %expected : i64
    %secondOK = llvm.icmp "eq" %second, %expected : i64
    %reentryOK = llvm.and %firstOK, %secondOK : i1
    %ok = llvm.and %firstOK, %secondOK : i1
    %status = llvm.select %ok, %zero, %one : i1, i32
    llvm.return %status : i32
  }
}
