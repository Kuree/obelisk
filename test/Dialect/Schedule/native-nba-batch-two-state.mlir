// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines -o %t.lowered
// RUN: FileCheck %s --implicit-check-not=native.nba_field --implicit-check-not=__talos_state_unknown < %t.lowered
// RUN: talos-translate --mlir-to-llvmir %t.lowered | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_clang %target_link_flags -O2 %t.o %S/Inputs/native-nba-batch-two-state.c -o %t.exe
// RUN: %t.exe

// IEEE 1800-2023 4.4.2.8, 4.5, 4.9.4: an NBA barrier cannot consume
// a pending Re-NBA. Exercise both retention and commit with the compact
// physical record, including a transient mask in the last word.
// CHECK: llvm.mlir.global external @__talos_aot_nba_accumulator_0() {{.*}}: !llvm.array<104 x i8>
// CHECK-NOT: !llvm.array<136 x i8>
// CHECK: llvm.func @specialized

#batch = #schedule.nba_batch_layout<plan = @plan, value = @value, unknown = @__talos_eval_nba_unknown_0, dirty = @dirty, summary = @summary, ingress = @ingress, bitCount = 256, rootCount = 1, capacity = 1>
#root = #schedule.nba_commit_layout<offset = 0, width = 256, accumulator = @__talos_aot_nba_accumulator_0, slots = [], consumers = []>
module attributes {
  native.two_state,
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  llvm.mlir.global external @plan() : i8
  llvm.mlir.global external @value() : !llvm.array<40 x i8>
  llvm.mlir.global external @__talos_eval_nba_unknown_0() : !llvm.array<40 x i8>
  llvm.mlir.global external @dirty() : i64
  llvm.mlir.global external @summary() : i64
  llvm.mlir.global external @ingress() : i64
  llvm.mlir.global external @__talos_aot_nba_accumulator_0() : !llvm.array<136 x i8>
  llvm.func @specialized(%ctx: !llvm.ptr, %region: i32, %changed: !llvm.ptr) {
    %status = schedule.nba.batch %ctx, %region, %changed {
      schedule.nba.commit 0 {layout = #root}
      schedule.nba.yield
    } {layout = #batch} : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
}
