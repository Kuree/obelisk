// RUN: obelisk-opt %s --finalize-native-llvm -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %s --finalize-native-llvm --mlir-disable-threading | diff %t.mlir -

// LRM 4.9.4/10.4.2: every captured value/XZ pair remains attached to its
// target through outlined calls and a canonical handoff, and is consumed once.
// Roots sit on both sides of the 64-root leaf boundaries and of the 4096-root
// summary-page boundary, so eight roots occupy five sparse bitmap words. A
// query helper with no direct accumulator access must still see the private
// index. Cold calls outside the eval closure adopt/export only their root
// scope. ../Runtime/simulation-native-eval-pending-sparse.test executes this
// self-checking program.
// CHECK: llvm.func internal @capture4096.__obelisk_pending
// CHECK-LABEL: llvm.func @capture4096()
// CHECK-NOT: llvm.mlir.addressof @acc0
// CHECK: llvm.mlir.addressof @acc4096
// CHECK-NOT: llvm.mlir.addressof @acc0
// CHECK: llvm.return
// CHECK: llvm.func internal @consume4096.__obelisk_pending
// CHECK-NOT: @canonical_only.__obelisk_pending
// CHECK-LABEL: llvm.func @canonical_only()
// CHECK: llvm.return
// CHECK: llvm.func internal @query.__obelisk_pending
// CHECK: llvm.func internal @pending4096.__obelisk_pending
// CHECK: llvm.func internal @__obelisk_eval_dispatch_v1.__obelisk_pending
// CHECK: llvm.call @obelisk_rt_v1_scheduler_execute_aot_actor
// CHECK: llvm.func @__obelisk_eval_dispatch_v1()
// 5 bitmap words + 8 value/XZ pairs + one transient word = 22 words.
// CHECK: llvm.mlir.constant(22 : i64)
// CHECK: llvm.alloca

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", schedule.eval.runtime_calendar, schedule.eval.transaction_roots = [{root = array<i64: 1, 0, 1, 0, 32, 4294967295, 2, 1>, accumulator = @acc0, sites = array<i64: 1>}, {root = array<i64: 2, 1, 2, 0, 32, 4294967295, 2, 0>, accumulator = @acc1, sites = array<i64: 2>}, {root = array<i64: 64, 63, 64, 0, 32, 4294967295, 2, 0>, accumulator = @acc63, sites = array<i64: 3>}, {root = array<i64: 65, 64, 65, 0, 32, 4294967295, 2, 0>, accumulator = @acc64, sites = array<i64: 4>}, {root = array<i64: 128, 127, 128, 0, 32, 4294967295, 2, 0>, accumulator = @acc127, sites = array<i64: 5>}, {root = array<i64: 129, 128, 129, 0, 32, 4294967295, 2, 0>, accumulator = @acc128, sites = array<i64: 6>}, {root = array<i64: 4096, 4095, 4096, 0, 32, 4294967295, 2, 0>, accumulator = @acc4095, sites = array<i64: 7>}, {root = array<i64: 4097, 4096, 4097, 0, 32, 4294967295, 2, 0>, accumulator = @acc4096, sites = array<i64: 8>}]} {
  llvm.mlir.global @acc0(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @acc1(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @acc63(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @acc64(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @acc127(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @acc128(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @acc4095(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @acc4096(dense<0> : tensor<136xi8>) : !llvm.array<136 x i8>
  llvm.mlir.global @__obelisk_aot_nba_dirty_roots_v1(dense<0> : tensor<520xi8>) : !llvm.array<520 x i8>
  llvm.mlir.global @__obelisk_aot_nba_dirty_summary_v1(dense<0> : tensor<16xi8>) : !llvm.array<16 x i8>
  llvm.mlir.global internal @__obelisk_current_context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.func @__obelisk_eval_write_guard_0_v1()
  llvm.func @obelisk_rt_v1_scheduler_execute_aot_actor(!llvm.ptr, i32) -> i32
  llvm.func @obelisk_rt_v1_scheduler_prepare_eval_nba(!llvm.ptr, !llvm.ptr) -> i32

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
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 0 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(1 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 0 : i32, schedule.eval.pending_stage_spec = array<i64: 0, 1, 32, 4294967295, 2, 1>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
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
  llvm.func @capture1() {
    %acc = llvm.mlir.addressof @acc1 : !llvm.ptr
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
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 1 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(2 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 1 : i32, schedule.eval.pending_stage_spec = array<i64: 1, 2, 32, 4294967295, 2, 2>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %page = llvm.getelementptr %summary[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %prior = llvm.load %page {schedule.eval.pending_summary_root = 1 : i32} : !llvm.ptr -> i64
    %pagebit = llvm.mlir.constant(1 : i64) : i64
    %markedpage = llvm.or %prior, %pagebit : i64
    llvm.store %markedpage, %page {schedule.eval.pending_summary_root = 1 : i32} : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @consume1() -> i64 {
    %acc = llvm.mlir.addressof @acc1 : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.load %valid {schedule.eval.pending_dirty_root = 1 : i32} : !llvm.ptr -> i32
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
  llvm.func @capture63() {
    %acc = llvm.mlir.addressof @acc63 : !llvm.ptr
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
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 63 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(9223372036854775808 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 63 : i32, schedule.eval.pending_stage_spec = array<i64: 63, 64, 32, 4294967295, 2, 3>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %page = llvm.getelementptr %summary[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %prior = llvm.load %page {schedule.eval.pending_summary_root = 63 : i32} : !llvm.ptr -> i64
    %pagebit = llvm.mlir.constant(1 : i64) : i64
    %markedpage = llvm.or %prior, %pagebit : i64
    llvm.store %markedpage, %page {schedule.eval.pending_summary_root = 63 : i32} : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @consume63() -> i64 {
    %acc = llvm.mlir.addressof @acc63 : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.load %valid {schedule.eval.pending_dirty_root = 63 : i32} : !llvm.ptr -> i32
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
    %value = llvm.mlir.constant(4 : i64) : i64
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
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 64 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(1 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 64 : i32, schedule.eval.pending_stage_spec = array<i64: 64, 65, 32, 4294967295, 2, 4>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
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
  llvm.func @capture127() {
    %acc = llvm.mlir.addressof @acc127 : !llvm.ptr
    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %region = llvm.getelementptr %acc[100] : (!llvm.ptr) -> !llvm.ptr, i8
    %transient = llvm.getelementptr %acc[104] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.mlir.constant(5 : i64) : i64
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
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 127 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(9223372036854775808 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 127 : i32, schedule.eval.pending_stage_spec = array<i64: 127, 128, 32, 4294967295, 2, 5>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %page = llvm.getelementptr %summary[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %prior = llvm.load %page {schedule.eval.pending_summary_root = 127 : i32} : !llvm.ptr -> i64
    %pagebit = llvm.mlir.constant(2 : i64) : i64
    %markedpage = llvm.or %prior, %pagebit : i64
    llvm.store %markedpage, %page {schedule.eval.pending_summary_root = 127 : i32} : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @consume127() -> i64 {
    %acc = llvm.mlir.addressof @acc127 : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.load %valid {schedule.eval.pending_dirty_root = 127 : i32} : !llvm.ptr -> i32
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
  llvm.func @capture128() {
    %acc = llvm.mlir.addressof @acc128 : !llvm.ptr
    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %region = llvm.getelementptr %acc[100] : (!llvm.ptr) -> !llvm.ptr, i8
    %transient = llvm.getelementptr %acc[104] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.mlir.constant(6 : i64) : i64
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
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[16] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 128 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(1 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 128 : i32, schedule.eval.pending_stage_spec = array<i64: 128, 129, 32, 4294967295, 2, 6>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %page = llvm.getelementptr %summary[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %prior = llvm.load %page {schedule.eval.pending_summary_root = 128 : i32} : !llvm.ptr -> i64
    %pagebit = llvm.mlir.constant(4 : i64) : i64
    %markedpage = llvm.or %prior, %pagebit : i64
    llvm.store %markedpage, %page {schedule.eval.pending_summary_root = 128 : i32} : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @consume128() -> i64 {
    %acc = llvm.mlir.addressof @acc128 : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.load %valid {schedule.eval.pending_dirty_root = 128 : i32} : !llvm.ptr -> i32
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
  llvm.func @capture4095() {
    %acc = llvm.mlir.addressof @acc4095 : !llvm.ptr
    %xz = llvm.getelementptr %acc[32] : (!llvm.ptr) -> !llvm.ptr, i8
    %mask = llvm.getelementptr %acc[64] : (!llvm.ptr) -> !llvm.ptr, i8
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %region = llvm.getelementptr %acc[100] : (!llvm.ptr) -> !llvm.ptr, i8
    %transient = llvm.getelementptr %acc[104] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.mlir.constant(7 : i64) : i64
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
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[504] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 4095 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(9223372036854775808 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 4095 : i32, schedule.eval.pending_stage_spec = array<i64: 4095, 4096, 32, 4294967295, 2, 7>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %page = llvm.getelementptr %summary[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %prior = llvm.load %page {schedule.eval.pending_summary_root = 4095 : i32} : !llvm.ptr -> i64
    %pagebit = llvm.mlir.constant(9223372036854775808 : i64) : i64
    %markedpage = llvm.or %prior, %pagebit : i64
    llvm.store %markedpage, %page {schedule.eval.pending_summary_root = 4095 : i32} : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @consume4095() -> i64 {
    %acc = llvm.mlir.addressof @acc4095 : !llvm.ptr
    %valid = llvm.getelementptr %acc[96] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.load %valid {schedule.eval.pending_dirty_root = 4095 : i32} : !llvm.ptr -> i32
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
    %value = llvm.mlir.constant(8 : i64) : i64
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
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[512] : (!llvm.ptr) -> !llvm.ptr, i8
    %old = llvm.load %leaf {schedule.eval.pending_dirty_root = 4096 : i32} : !llvm.ptr -> i64
    %bit = llvm.mlir.constant(1 : i64) : i64
    %marked = llvm.or %old, %bit : i64
    llvm.store %marked, %leaf {schedule.eval.pending_dirty_root = 4096 : i32, schedule.eval.pending_stage_spec = array<i64: 4096, 4097, 32, 4294967295, 2, 8>} : i64, !llvm.ptr
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
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
    %summary = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %bits = llvm.load %summary {schedule.eval.pending_runtime_root = 5000 : i32} : !llvm.ptr -> i64
    llvm.return %bits : i64
  }
  llvm.func @query() -> i64 {
    %zero = llvm.mlir.constant(0 : i64) : i64
    %base0 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr0 = llvm.getelementptr %base0[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits0 = llvm.load %ptr0 : !llvm.ptr -> i64
    %sum0 = llvm.xor %zero, %bits0 : i64
    %base1 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr1 = llvm.getelementptr %base1[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits1 = llvm.load %ptr1 : !llvm.ptr -> i64
    %sum1 = llvm.xor %sum0, %bits1 : i64
    %base2 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr2 = llvm.getelementptr %base2[16] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits2 = llvm.load %ptr2 : !llvm.ptr -> i64
    %sum2 = llvm.xor %sum1, %bits2 : i64
    %base3 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr3 = llvm.getelementptr %base3[504] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits3 = llvm.load %ptr3 : !llvm.ptr -> i64
    %sum3 = llvm.xor %sum2, %bits3 : i64
    %base4 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr4 = llvm.getelementptr %base4[512] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits4 = llvm.load %ptr4 : !llvm.ptr -> i64
    %sum4 = llvm.xor %sum3, %bits4 : i64
    %base5 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %ptr5 = llvm.getelementptr %base5[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits5 = llvm.load %ptr5 : !llvm.ptr -> i64
    %sum5 = llvm.xor %sum4, %bits5 : i64
    %base6 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %ptr6 = llvm.getelementptr %base6[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits6 = llvm.load %ptr6 : !llvm.ptr -> i64
    %sum6 = llvm.xor %sum5, %bits6 : i64
    llvm.return %sum6 : i64
  }
  llvm.func @pending4096() -> i64 {
    %dirty = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %leaf = llvm.getelementptr %dirty[512] : (!llvm.ptr) -> !llvm.ptr, i8
    %bits = llvm.load %leaf {schedule.eval.pending_dirty_root = 4096 : i32} : !llvm.ptr -> i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %result = llvm.and %bits, %one : i64
    llvm.return %result : i64
  }
  llvm.func @__obelisk_eval_dispatch_v1() -> i64 attributes {schedule.eval.call_closure_root} {
    %null = llvm.mlir.zero : !llvm.ptr
    %slot = llvm.mlir.constant(0 : i32) : i32
    %zero = llvm.mlir.constant(0 : i64) : i64
    llvm.call @capture0() : () -> ()
    llvm.call @capture1() : () -> ()
    llvm.call @capture63() : () -> ()
    llvm.call @capture64() : () -> ()
    llvm.call @capture127() : () -> ()
    llvm.call @capture128() : () -> ()
    llvm.call @capture4095() : () -> ()
    llvm.call @capture4096() : () -> ()
    %canonical = llvm.call @canonical_only() : () -> i64
    %before = llvm.call @query() : () -> i64
    %expectedQuery = llvm.mlir.constant(4 : i64) : i64
    %queryOK = llvm.icmp "eq" %before, %expectedQuery : i64
    %last = llvm.call @pending4096() : () -> i64
    %one64 = llvm.mlir.constant(1 : i64) : i64
    %lastOK = llvm.icmp "eq" %last, %one64 : i64
    %status = llvm.call @obelisk_rt_v1_scheduler_execute_aot_actor(%null, %slot) : (!llvm.ptr, i32) -> i32
    %zero32 = llvm.mlir.constant(0 : i32) : i32
    %cutOK = llvm.icmp "eq" %status, %zero32 : i32
    %value0 = llvm.call @consume0() : () -> i64
    %sum0 = llvm.add %zero, %value0 : i64
    %value1 = llvm.call @consume1() : () -> i64
    %sum1 = llvm.add %sum0, %value1 : i64
    %value2 = llvm.call @consume63() : () -> i64
    %sum2 = llvm.add %sum1, %value2 : i64
    %value3 = llvm.call @consume64() : () -> i64
    %sum3 = llvm.add %sum2, %value3 : i64
    %value4 = llvm.call @consume127() : () -> i64
    %sum4 = llvm.add %sum3, %value4 : i64
    %value5 = llvm.call @consume128() : () -> i64
    %sum5 = llvm.add %sum4, %value5 : i64
    %value6 = llvm.call @consume4095() : () -> i64
    %sum6 = llvm.add %sum5, %value6 : i64
    %value7 = llvm.call @consume4096() : () -> i64
    %sum7 = llvm.add %sum6, %value7 : i64
    %base0 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr0 = llvm.getelementptr %base0[0] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr0 : i64, !llvm.ptr
    %base1 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr1 = llvm.getelementptr %base1[8] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr1 : i64, !llvm.ptr
    %base2 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr2 = llvm.getelementptr %base2[16] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr2 : i64, !llvm.ptr
    %base3 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr3 = llvm.getelementptr %base3[504] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr3 : i64, !llvm.ptr
    %base4 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1 : !llvm.ptr
    %ptr4 = llvm.getelementptr %base4[512] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr4 : i64, !llvm.ptr
    %base5 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %ptr5 = llvm.getelementptr %base5[0] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr5 : i64, !llvm.ptr
    %base6 = llvm.mlir.addressof @__obelisk_aot_nba_dirty_summary_v1 : !llvm.ptr
    %ptr6 = llvm.getelementptr %base6[8] : (!llvm.ptr) -> !llvm.ptr, i8
    llvm.store %zero, %ptr6 : i64, !llvm.ptr
    %after = llvm.call @query() : () -> i64
    %empty = llvm.icmp "eq" %after, %zero : i64
    %validCut = llvm.and %queryOK, %cutOK : i1
    %validLast = llvm.and %validCut, %lastOK : i1
    %valid = llvm.and %validLast, %empty : i1
    %failure = llvm.mlir.constant(-1 : i64) : i64
    %result = llvm.select %valid, %sum7, %failure : i1, i64
    llvm.return %result : i64
  }
  llvm.func @main() -> i32 {
    %first = llvm.call @__obelisk_eval_dispatch_v1() : () -> i64
    %second = llvm.call @__obelisk_eval_dispatch_v1() : () -> i64
    %expected = llvm.mlir.constant(292 : i64) : i64
    %one = llvm.mlir.constant(1 : i32) : i32
    %zero = llvm.mlir.constant(0 : i32) : i32
    %firstOK = llvm.icmp "eq" %first, %expected : i64
    %secondOK = llvm.icmp "eq" %second, %expected : i64
    %reentryOK = llvm.and %firstOK, %secondOK : i1
    llvm.call @capture4096() : () -> ()
    %pending = llvm.call @pending4096() : () -> i64
    %cold = llvm.call @consume4096() : () -> i64
    %again = llvm.call @consume4096() : () -> i64
    %one64 = llvm.mlir.constant(1 : i64) : i64
    %zero64 = llvm.mlir.constant(0 : i64) : i64
    %coldExpected = llvm.mlir.constant(8 : i64) : i64
    %pendingOK = llvm.icmp "eq" %pending, %one64 : i64
    %coldOK = llvm.icmp "eq" %cold, %coldExpected : i64
    %emptyOK = llvm.icmp "eq" %again, %zero64 : i64
    %coldCaptureOK = llvm.and %pendingOK, %coldOK : i1
    %coldOnceOK = llvm.and %coldCaptureOK, %emptyOK : i1
    %ok = llvm.and %reentryOK, %coldOnceOK : i1
    %status = llvm.select %ok, %zero, %one : i1, i32
    llvm.return %status : i32
  }
}
