; REQUIRES: llvm-pass-plugin
; RUN: %llvm_dist/bin/opt -load-pass-plugin=%pass_plugin -passes='function(talos-limit-wide-integer-vectorization)' -S %s | FileCheck %s
; RUN: %llvm_dist/bin/opt -load-pass-plugin=%pass_plugin -passes='function(talos-limit-wide-integer-vectorization,talos-limit-wide-integer-vectorization)' -S %s | FileCheck %s
; RUN: %llvm_dist/bin/opt -load-pass-plugin=%pass_plugin -mtriple=%target_triple -passes='default<O3>' -S %s | FileCheck %s --check-prefix=OPT

; Packed selections can become loops of shifts on multi-kilobit integers.
; The x86 vectorizer otherwise creates <32 x i8350>, whose legalization and
; scheduling dominate compilation. Keep these loops scalar and preserve other
; loop hints. Ordinary integer loops still vectorize through the same pipeline.

; CHECK-LABEL: define i32 @wide_shift(
; OPT-LABEL: define i32 @wide_shift(
; OPT-NOT: x i8350>
; OPT: ret i32
; OPT-NOT: x i8350>
define i32 @wide_shift(i8350 %bits) {
entry:
  br label %loop
loop:
  %index = phi i32 [ 0, %entry ], [ %next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %total, %loop ]
  %offset = mul nuw nsw i32 %index, 16
  %amount = zext i32 %offset to i8350
  %shifted = lshr i8350 %bits, %amount
  %piece = trunc i8350 %shifted to i32
  %total = add i32 %sum, %piece
  %next = add nuw nsw i32 %index, 1
  %more = icmp ult i32 %next, 520
  ; CHECK: br i1 %more, label %loop, label %exit, !llvm.loop ![[WIDE:[0-9]+]]
  br i1 %more, label %loop, label %exit, !llvm.loop !0
exit:
  ret i32 %total
}

; CHECK-LABEL: define void @ordinary_shift(
; OPT-LABEL: define void @ordinary_shift(
; OPT: load <{{[0-9]+}} x i32>
; OPT: lshr <{{[0-9]+}} x i32>
define void @ordinary_shift(ptr noalias %output, ptr noalias %input, i32 %amount) {
entry:
  br label %loop
loop:
  %index = phi i64 [ 0, %entry ], [ %next, %loop ]
  %source = getelementptr i32, ptr %input, i64 %index
  %value = load i32, ptr %source, align 4
  %shifted = lshr i32 %value, %amount
  %destination = getelementptr i32, ptr %output, i64 %index
  store i32 %shifted, ptr %destination, align 4
  %next = add nuw nsw i64 %index, 1
  %more = icmp ult i64 %next, 520
  ; CHECK: br i1 %more, label %loop, label %exit{{$}}
  br i1 %more, label %loop, label %exit
exit:
  ret void
}

; Constant wide shifts and variable shifts of up to two words retain LLVM's
; vectorization policy. These helpers use volatile stores to keep the shapes.
; CHECK-LABEL: define void @constant_wide_shift(
define void @constant_wide_shift(ptr %output, i8350 %input, i32 %count) {
entry:
  br label %loop
loop:
  %index = phi i32 [ 0, %entry ], [ %next, %loop ]
  %value = phi i8350 [ %input, %entry ], [ %shifted, %loop ]
  %shifted = ashr i8350 %value, 1
  store volatile i8350 %shifted, ptr %output, align 1
  %next = add i32 %index, 1
  %more = icmp ult i32 %next, %count
  ; CHECK: br i1 %more, label %loop, label %exit{{$}}
  br i1 %more, label %loop, label %exit
exit:
  ret void
}

; CHECK-LABEL: define void @two_word_shift(
define void @two_word_shift(ptr %output, i128 %input, i128 %amount, i32 %count) {
entry:
  br label %loop
loop:
  %index = phi i32 [ 0, %entry ], [ %next, %loop ]
  %value = phi i128 [ %input, %entry ], [ %shifted, %loop ]
  %shifted = shl i128 %value, %amount
  store volatile i128 %shifted, ptr %output, align 16
  %next = add i32 %index, 1
  %more = icmp ult i32 %next, %count
  ; CHECK: br i1 %more, label %loop, label %exit{{$}}
  br i1 %more, label %loop, label %exit
exit:
  ret void
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.unroll.disable"}

; CHECK: ![[WIDE]] = distinct !{![[WIDE]], ![[UNROLL:[0-9]+]], ![[DISABLE:[0-9]+]]}
; CHECK: ![[UNROLL]] = !{!"llvm.loop.unroll.disable"}
; CHECK: ![[DISABLE]] = !{!"llvm.loop.vectorize.enable", i32 0}
