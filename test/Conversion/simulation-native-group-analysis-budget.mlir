// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-translate --mlir-to-llvmir %t.mlir | %llvm_dist/bin/opt -passes='default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang %t.o -o %t.exe
// RUN: %t.exe
// RUN: obelisk-translate --mlir-to-llvmir %t.mlir | %llvm_dist/bin/opt -passes='default<O3>' | %llc -filetype=obj -relocation-model=pic -o %t.o3
// RUN: %llvm_dist/bin/clang %t.o3 -o %t.o3.exe
// RUN: %t.o3.exe

// Many exact storage slots cross a finite CFG whose actual computation is small.
// Keep retained values on untaken paths; charge code growth independently of
// lattice bookkeeping. The fallback deliberately returns incorrect state.
// CHECK-LABEL: llvm.func @group()
// CHECK-SAME: schedule.eval.predicated_dataflow
// CHECK-LABEL: llvm.func @group.fallback()
module attributes {schedule.native.max_inline_ops = 120 : i64} {
  llvm.mlir.global internal @__obelisk_state_value(dense<9> : tensor<8xi8>) : !llvm.array<8 x i8>
  llvm.mlir.global internal @ready(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready} {
    %p = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %bad = llvm.mlir.constant(99 : i8) : i8
    llvm.store %bad, %p : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @candidate() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %mask = llvm.load %ready : !llvm.ptr -> i64
    %zero = llvm.mlir.constant(0 : i64) : i64
    %value = llvm.mlir.constant(3 : i8) : i8
    %p0 = llvm.getelementptr %base[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %p1 = llvm.getelementptr %base[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %p2 = llvm.getelementptr %base[2] : (!llvm.ptr) -> !llvm.ptr, i8
    %p3 = llvm.getelementptr %base[3] : (!llvm.ptr) -> !llvm.ptr, i8
    %p4 = llvm.getelementptr %base[4] : (!llvm.ptr) -> !llvm.ptr, i8
    %p5 = llvm.getelementptr %base[5] : (!llvm.ptr) -> !llvm.ptr, i8
    %p6 = llvm.getelementptr %base[6] : (!llvm.ptr) -> !llvm.ptr, i8
    %p7 = llvm.getelementptr %base[7] : (!llvm.ptr) -> !llvm.ptr, i8
    %bit0 = llvm.mlir.constant(1 : i64) : i64
    %set0 = llvm.and %mask, %bit0 : i64
    %active0 = llvm.icmp "ne" %set0, %zero : i64
    llvm.cond_br %active0, ^write0, ^next0
  ^write0:
    llvm.store %value, %p0 : i8, !llvm.ptr
    llvm.br ^next0
  ^next0:
    %bit1 = llvm.mlir.constant(2 : i64) : i64
    %set1 = llvm.and %mask, %bit1 : i64
    %active1 = llvm.icmp "ne" %set1, %zero : i64
    llvm.cond_br %active1, ^write1, ^next1
  ^write1:
    llvm.store %value, %p1 : i8, !llvm.ptr
    llvm.br ^next1
  ^next1:
    %bit2 = llvm.mlir.constant(4 : i64) : i64
    %set2 = llvm.and %mask, %bit2 : i64
    %active2 = llvm.icmp "ne" %set2, %zero : i64
    llvm.cond_br %active2, ^write2, ^next2
  ^write2:
    llvm.store %value, %p2 : i8, !llvm.ptr
    llvm.br ^next2
  ^next2:
    %bit3 = llvm.mlir.constant(8 : i64) : i64
    %set3 = llvm.and %mask, %bit3 : i64
    %active3 = llvm.icmp "ne" %set3, %zero : i64
    llvm.cond_br %active3, ^write3, ^next3
  ^write3:
    llvm.store %value, %p3 : i8, !llvm.ptr
    llvm.br ^next3
  ^next3:
    %bit4 = llvm.mlir.constant(16 : i64) : i64
    %set4 = llvm.and %mask, %bit4 : i64
    %active4 = llvm.icmp "ne" %set4, %zero : i64
    llvm.cond_br %active4, ^write4, ^next4
  ^write4:
    llvm.store %value, %p4 : i8, !llvm.ptr
    llvm.br ^next4
  ^next4:
    %bit5 = llvm.mlir.constant(32 : i64) : i64
    %set5 = llvm.and %mask, %bit5 : i64
    %active5 = llvm.icmp "ne" %set5, %zero : i64
    llvm.cond_br %active5, ^write5, ^next5
  ^write5:
    llvm.store %value, %p5 : i8, !llvm.ptr
    llvm.br ^next5
  ^next5:
    %bit6 = llvm.mlir.constant(64 : i64) : i64
    %set6 = llvm.and %mask, %bit6 : i64
    %active6 = llvm.icmp "ne" %set6, %zero : i64
    llvm.cond_br %active6, ^write6, ^next6
  ^write6:
    llvm.store %value, %p6 : i8, !llvm.ptr
    llvm.br ^next6
  ^next6:
    %bit7 = llvm.mlir.constant(128 : i64) : i64
    %set7 = llvm.and %mask, %bit7 : i64
    %active7 = llvm.icmp "ne" %set7, %zero : i64
    llvm.cond_br %active7, ^write7, ^next7
  ^write7:
    llvm.store %value, %p7 : i8, !llvm.ptr
    llvm.br ^next7
  ^next7:
    llvm.return
  }
  llvm.func @main() -> i32 {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %mask = llvm.mlir.constant(85 : i64) : i64
    llvm.store %mask, %ready : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %yes = llvm.mlir.constant(true) : i1
    %p0 = llvm.getelementptr %base[0] : (!llvm.ptr) -> !llvm.ptr, i8
    %v0 = llvm.load %p0 : !llvm.ptr -> i8
    %e0 = llvm.mlir.constant(3 : i8) : i8
    %ok0 = llvm.icmp "eq" %v0, %e0 : i8
    %all0 = llvm.and %ok0, %yes : i1
    %p1 = llvm.getelementptr %base[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %v1 = llvm.load %p1 : !llvm.ptr -> i8
    %e1 = llvm.mlir.constant(9 : i8) : i8
    %ok1 = llvm.icmp "eq" %v1, %e1 : i8
    %all1 = llvm.and %ok1, %all0 : i1
    %p2 = llvm.getelementptr %base[2] : (!llvm.ptr) -> !llvm.ptr, i8
    %v2 = llvm.load %p2 : !llvm.ptr -> i8
    %e2 = llvm.mlir.constant(3 : i8) : i8
    %ok2 = llvm.icmp "eq" %v2, %e2 : i8
    %all2 = llvm.and %ok2, %all1 : i1
    %p3 = llvm.getelementptr %base[3] : (!llvm.ptr) -> !llvm.ptr, i8
    %v3 = llvm.load %p3 : !llvm.ptr -> i8
    %e3 = llvm.mlir.constant(9 : i8) : i8
    %ok3 = llvm.icmp "eq" %v3, %e3 : i8
    %all3 = llvm.and %ok3, %all2 : i1
    %p4 = llvm.getelementptr %base[4] : (!llvm.ptr) -> !llvm.ptr, i8
    %v4 = llvm.load %p4 : !llvm.ptr -> i8
    %e4 = llvm.mlir.constant(3 : i8) : i8
    %ok4 = llvm.icmp "eq" %v4, %e4 : i8
    %all4 = llvm.and %ok4, %all3 : i1
    %p5 = llvm.getelementptr %base[5] : (!llvm.ptr) -> !llvm.ptr, i8
    %v5 = llvm.load %p5 : !llvm.ptr -> i8
    %e5 = llvm.mlir.constant(9 : i8) : i8
    %ok5 = llvm.icmp "eq" %v5, %e5 : i8
    %all5 = llvm.and %ok5, %all4 : i1
    %p6 = llvm.getelementptr %base[6] : (!llvm.ptr) -> !llvm.ptr, i8
    %v6 = llvm.load %p6 : !llvm.ptr -> i8
    %e6 = llvm.mlir.constant(3 : i8) : i8
    %ok6 = llvm.icmp "eq" %v6, %e6 : i8
    %all6 = llvm.and %ok6, %all5 : i1
    %p7 = llvm.getelementptr %base[7] : (!llvm.ptr) -> !llvm.ptr, i8
    %v7 = llvm.load %p7 : !llvm.ptr -> i8
    %e7 = llvm.mlir.constant(9 : i8) : i8
    %ok7 = llvm.icmp "eq" %v7, %e7 : i8
    %all7 = llvm.and %ok7, %all6 : i1
    %zero = llvm.mlir.constant(0 : i32) : i32
    %one = llvm.mlir.constant(1 : i32) : i32
    %result = llvm.select %all7, %zero, %one : i1, i32
    llvm.return %result : i32
  }
}
