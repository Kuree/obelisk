// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.mlir | %llvm_dist/bin/opt -passes='default<O0>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang %t.o -o %t.exe
// RUN: %t.exe
// RUN: mlir-translate --mlir-to-llvmir %t.mlir | %llvm_dist/bin/opt -passes='default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o3
// RUN: %llvm_dist/bin/clang %t.o3 -o %t.o3.exe
// RUN: %t.o3.exe

// A deposit at a real boundary need not satisfy a combinational invariant.
// With only consumer activation, the producer must retain its deposited value.
// A conditionally assigned output must also hold when its branch is closed.
// For input 7 the inactive assignment's shift produces poison. Its branch
// condition must not poison the enclosing reconvergence or retained state.
// CHECK-LABEL: llvm.func @group()
// CHECK-SAME: obelisk.eval.cache_hint_words = 1 : i64
// CHECK-SAME: obelisk.eval.dataflow_fallback = @group.fallback
// CHECK-SAME: obelisk.eval.predicated_dataflow
// CHECK: llvm.cond_br
// CHECK-NOT: llvm.cond_br
// CHECK-NOT: llvm.br
// CHECK-NOT: llvm.call
// CHECK: llvm.select
// CHECK-NOT: llvm.cond_br
// CHECK-NOT: llvm.br
// CHECK-NOT: llvm.call
// CHECK: llvm.return
// CHECK: llvm.call @group.fallback()
// CHECK-LABEL: llvm.func @group.fallback()
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.mlir.global internal @__obelisk_state_value(dense<[7, 99, 0]> : tensor<3xi8>) : !llvm.array<3 x i8>
  llvm.mlir.global internal @ready(dense<[2, 1, 0]> : tensor<3xi64>) : !llvm.array<3 x i64>
  llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1(dense<0> : tensor<2xi64>) : !llvm.array<2 x i64>
  llvm.mlir.global internal @slow_count(0 : i32) : i32
  llvm.func @group() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.ready_word_count = 2 : i64} {
    %p = llvm.mlir.addressof @slow_count : !llvm.ptr
    %one = llvm.mlir.constant(1 : i32) : i32
    llvm.store %one, %p : i32, !llvm.ptr
    llvm.return
  }
  llvm.func @dataflow() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group, obelisk.eval.ready_word_count = 2 : i64} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %middle = llvm.getelementptr %base[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %output = llvm.getelementptr %base[2] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %cache = llvm.getelementptr %ready[16] : (!llvm.ptr) -> !llvm.ptr, i8
    %lower = llvm.load %cache : !llvm.ptr -> i64
    llvm.store %lower, %cache : i64, !llvm.ptr
    %one = llvm.mlir.constant(1 : i64) : i64
    %two = llvm.mlir.constant(2 : i64) : i64
    %zero = llvm.mlir.constant(0 : i64) : i64
    %clear0 = llvm.mlir.constant(-2 : i64) : i64
    %clear1 = llvm.mlir.constant(-3 : i64) : i64
    %one8 = llvm.mlir.constant(1 : i8) : i8
    %zero8 = llvm.mlir.constant(0 : i8) : i8
    %r = llvm.load %ready : !llvm.ptr -> i64
    %bit0 = llvm.and %r, %one : i64
    %active0 = llvm.icmp "ne" %bit0, %zero : i64
    llvm.cond_br %active0, ^producer, ^consumer_check
  ^producer:
    %consumed = llvm.and %r, %clear0 : i64
    llvm.store %consumed, %ready : i64, !llvm.ptr
    %input = llvm.load %base : !llvm.ptr -> i8
    %low = llvm.and %input, %one8 : i8
    %open = llvm.icmp "eq" %low, %zero8 : i8
    llvm.cond_br %open, ^assign, ^consumer_check
  ^assign:
    %next = llvm.add %input, %one8 : i8
    %shifted = llvm.shl %one8, %next : i8
    %allowed = llvm.icmp "ne" %shifted, %zero8 : i8
    llvm.cond_br %allowed, ^assign_value, ^consumer_check
  ^assign_value:
    llvm.store %next, %middle : i8, !llvm.ptr
    %published = llvm.or %consumed, %two : i64
    llvm.store %published, %ready : i64, !llvm.ptr
    llvm.br ^consumer_check
  ^consumer_check:
    %pending = llvm.load %ready : !llvm.ptr -> i64
    %bit1 = llvm.and %pending, %two : i64
    %active1 = llvm.icmp "ne" %bit1, %zero : i64
    llvm.cond_br %active1, ^consumer, ^done
  ^consumer:
    %mid = llvm.load %middle : !llvm.ptr -> i8
    %selector = llvm.load %base : !llvm.ptr -> i8
    %odd = llvm.and %selector, %one8 : i8
    %choose = llvm.icmp "ne" %odd, %zero8 : i8
    llvm.cond_br %choose, ^left, ^right
  ^left:
    %result = llvm.add %mid, %one8 : i8
    llvm.br ^selected(%result : i8)
  ^right:
    %two8 = llvm.mlir.constant(2 : i8) : i8
    %other = llvm.add %mid, %two8 : i8
    llvm.br ^selected(%other : i8)
  ^selected(%selected: i8):
    llvm.store %selected, %output : i8, !llvm.ptr
    %remaining = llvm.and %pending, %clear1 : i64
    llvm.store %remaining, %ready : i64, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return
  }
  llvm.func @main() -> i32 {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %middle = llvm.getelementptr %base[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %output = llvm.getelementptr %base[2] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %outside = llvm.getelementptr %ready[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %cache = llvm.getelementptr %ready[16] : (!llvm.ptr) -> !llvm.ptr, i8
    %one64 = llvm.mlir.constant(1 : i64) : i64
    %two64 = llvm.mlir.constant(2 : i64) : i64
    %zero64 = llvm.mlir.constant(0 : i64) : i64
    %six = llvm.mlir.constant(6 : i8) : i8
    %seven = llvm.mlir.constant(7 : i8) : i8
    %nine = llvm.mlir.constant(9 : i8) : i8
    %n99 = llvm.mlir.constant(99 : i8) : i8
    %n100 = llvm.mlir.constant(100 : i8) : i8
    %n33 = llvm.mlir.constant(33 : i8) : i8
    %n34 = llvm.mlir.constant(34 : i8) : i8
    llvm.call @group() : () -> ()
    // Word zero has been consumed. The group does not inspect word one, so
    // the hint must stop there and preserve that external pending actor.
    %hint = llvm.load %cache : !llvm.ptr -> i64
    %hintok = llvm.icmp "eq" %hint, %one64 : i64
    %mid1 = llvm.load %middle : !llvm.ptr -> i8
    %out1 = llvm.load %output : !llvm.ptr -> i8
    %a = llvm.icmp "eq" %mid1, %n99 : i8
    %b = llvm.icmp "eq" %out1, %n100 : i8
    %ok1 = llvm.and %a, %b : i1
    llvm.store %six, %base : i8, !llvm.ptr
    llvm.store %one64, %ready : i64, !llvm.ptr
    llvm.store %zero64, %cache : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %out2 = llvm.load %output : !llvm.ptr -> i8
    %ok2 = llvm.icmp "eq" %out2, %nine : i8
    llvm.store %seven, %base : i8, !llvm.ptr
    llvm.store %one64, %ready : i64, !llvm.ptr
    llvm.store %zero64, %cache : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %mid3 = llvm.load %middle : !llvm.ptr -> i8
    %out3 = llvm.load %output : !llvm.ptr -> i8
    %c = llvm.icmp "eq" %mid3, %seven : i8
    %d = llvm.icmp "eq" %out3, %nine : i8
    %ok3 = llvm.and %c, %d : i1
    llvm.store %n33, %middle : i8, !llvm.ptr
    llvm.store %two64, %ready : i64, !llvm.ptr
    llvm.store %zero64, %cache : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %out4 = llvm.load %output : !llvm.ptr -> i8
    %ok4 = llvm.icmp "eq" %out4, %n34 : i8
    %pending = llvm.load %ready : !llvm.ptr -> i64
    %ok5 = llvm.icmp "eq" %pending, %zero64 : i64
    // An unowned bit in the same word prevents hint advancement. It must
    // neither be consumed nor hidden by a cache pointing past that word.
    %four64 = llvm.mlir.constant(4 : i64) : i64
    llvm.store %four64, %ready : i64, !llvm.ptr
    llvm.store %zero64, %cache : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %held = llvm.load %ready : !llvm.ptr -> i64
    %heldHint = llvm.load %cache : !llvm.ptr -> i64
    %external = llvm.load %outside : !llvm.ptr -> i64
    %heldok = llvm.icmp "eq" %held, %four64 : i64
    %heldHintok = llvm.icmp "eq" %heldHint, %zero64 : i64
    %externalok = llvm.icmp "eq" %external, %one64 : i64
    %hintpair = llvm.and %hintok, %heldHintok : i1
    %readyPair = llvm.and %heldok, %externalok : i1
    %cacheok = llvm.and %hintpair, %readyPair : i1
    %slow = llvm.mlir.addressof @slow_count : !llvm.ptr
    %count = llvm.load %slow : !llvm.ptr -> i32
    %zero = llvm.mlir.constant(0 : i32) : i32
    %failure = llvm.mlir.constant(1 : i32) : i32
    %ok6 = llvm.icmp "eq" %count, %zero : i32
    %ok12 = llvm.and %ok1, %ok2 : i1
    %ok34 = llvm.and %ok3, %ok4 : i1
    %ok56 = llvm.and %ok5, %ok6 : i1
    %ok1234 = llvm.and %ok12, %ok34 : i1
    %ok = llvm.and %ok1234, %ok56 : i1
    %allok = llvm.and %ok, %cacheok : i1
    %status = llvm.select %allok, %zero, %failure : i1, i32
    llvm.return %status : i32
  }
}
