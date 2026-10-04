// RUN: obelisk-opt %s --convert-native-schedule-actions-to-llvm -o %t.lowered
// RUN: FileCheck %s --implicit-check-not="llvm.mlir.addressof @constant_offset" --implicit-check-not=llvm.intr.cttz --implicit-check-not=llvm.switch < %t.lowered
// RUN: %llvm_dist/bin/mlir-opt %t.lowered --convert-arith-to-llvm --convert-cf-to-llvm | mlir-translate --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang -O2 %t.o %S/Inputs/native-nba-batch.c -o %t.exe
// RUN: %t.exe

// The layout survives as an explicit region certificate until conversion.
// Conversion selects dirty roots, emits their stores, and groups readiness.
// CHECK-NOT: llvm.call @obelisk_rt_v1_static_nba_commit_batch(
// CHECK: llvm.func @specialized
// CHECK: llvm.call @obelisk_rt_v1_static_nba_batch_guard
// CHECK: cf.cond_br
// CHECK: llvm.call @obelisk_rt_v1_static_nba_batch_knownness
// CHECK: llvm.call @obelisk_rt_v1_static_nba_batch_finish
// CHECK-NOT: schedule.nba
#batch = #schedule.nba_batch_layout<plan = @plan, value = @value, unknown = @unknown, dirty = @dirty, summary = @summary, ingress = @ingress, bitCount = 256, rootCount = 66, capacity = 130>
#consumer = #schedule.nba_consumer<low = 64, width = 65, bit = 129>
#slot_consumer = #schedule.nba_consumer<low = 75, width = 5, bit = 64>
#slot = #schedule.nba_slot_layout<offset = @offset, value = @staged_value, unknown = @staged_unknown, valid = @valid, width = 64>
#wide = #schedule.nba_commit_layout<offset = 3, width = 129, accumulator = @accumulator, slots = [], consumers = [#consumer]>
#constant_slot = #schedule.nba_slot_layout<offset = @constant_offset, value = @staged_value, unknown = @staged_unknown, valid = @constant_valid, width = 5, constantOffset = 3 : i64>
#constant = #schedule.nba_commit_layout<offset = 224, width = 8, slots = [#constant_slot], consumers = []>
#aux2 = #schedule.nba_commit_layout<offset = 232, width = 8, accumulator = @aux2, slots = [], consumers = []>
#aux63 = #schedule.nba_commit_layout<offset = 240, width = 8, accumulator = @aux63, slots = [], consumers = []>
#fixed = #schedule.nba_commit_layout<offset = 144, width = 80, slots = [#slot], consumers = [#slot_consumer]>
module {
  llvm.mlir.global external @plan() : i8
  llvm.mlir.global external @value() : !llvm.array<40 x i8>
  llvm.mlir.global external @unknown() : !llvm.array<40 x i8>
  llvm.mlir.global external @dirty() : !llvm.array<2 x i64>
  llvm.mlir.global external @summary() : i64
  llvm.mlir.global external @ingress() : !llvm.array<8 x i64>
  llvm.mlir.global external @accumulator() : !llvm.array<136 x i8>
  llvm.mlir.global external @aux2() : !llvm.array<136 x i8>
  llvm.mlir.global external @aux63() : !llvm.array<136 x i8>
  llvm.mlir.global external @constant_offset() : i64
  llvm.mlir.global external @constant_valid() : i32
  llvm.mlir.global external @offset() : i64
  llvm.mlir.global external @staged_value() : i64
  llvm.mlir.global external @staged_unknown() : i64
  llvm.mlir.global external @valid() : i32
  llvm.func @specialized(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    %status = schedule.nba.batch %ctx, %region, %changed {
      schedule.nba.commit 1 {layout = #wide}
      schedule.nba.commit 2 {layout = #aux2}
      schedule.nba.commit 3 {layout = #constant}
      schedule.nba.commit 63 {layout = #aux63}
      schedule.nba.commit 65 {layout = #fixed}
      schedule.nba.yield
    } {layout = #batch} : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
