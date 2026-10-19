; RUN: %llvm_dist/bin/opt -load-pass-plugin=%pass_plugin -passes=obelisk-clocked-guard-layout -S %s | FileCheck %s

; One-armed guards in a clocked kernel body get an unlikely arm edge in either
; orientation, including an arm with internal control flow. A diamond, an
; existing weight, an arm with a second predecessor, an arm that leaves the
; function, and every function outside a clocked kernel keep their layout.

; CHECK-LABEL: define i32 @__obelisk_clocked_3_1_0.__obelisk_eval_body_0
define i32 @__obelisk_clocked_3_1_0.__obelisk_eval_body_0(ptr %state, i1 %c0, i1 %c1, i1 %c2, i1 %c3, i1 %c4, i1 %c5, i1 %c6, i1 %c7) {
entry:
  ; CHECK: br i1 %c0, label %arm0, label %join0, !prof ![[UNLIKELY_TRUE:[0-9]+]]
  br i1 %c0, label %arm0, label %join0
arm0:
  store i8 1, ptr %state
  br label %join0
join0:
  ; CHECK: br i1 %c1, label %join1, label %arm1, !prof ![[UNLIKELY_FALSE:[0-9]+]]
  br i1 %c1, label %join1, label %arm1
arm1:
  store i8 2, ptr %state
  br label %join1
join1:
  ; The arm holds a nested guard; the whole region is the arm.
  ; CHECK: br i1 %c2, label %arm2, label %join2, !prof ![[UNLIKELY_TRUE]]
  br i1 %c2, label %arm2, label %join2
arm2:
  store i8 3, ptr %state
  ; CHECK: br i1 %c3, label %arm2.nested, label %join2, !prof ![[UNLIKELY_TRUE]]
  br i1 %c3, label %arm2.nested, label %join2
arm2.nested:
  store i8 4, ptr %state
  br label %join2
join2:
  ; A diamond selects between two arms.
  ; CHECK: br i1 %c4, label %left, label %right{{$}}
  br i1 %c4, label %left, label %right
left:
  store i8 5, ptr %state
  br label %join4
right:
  store i8 6, ptr %state
  br label %join4
join4:
  ; An explicit weight is kept.
  ; CHECK: br i1 %c5, label %arm5, label %join5, !prof ![[GIVEN:[0-9]+]]
  br i1 %c5, label %arm5, label %join5, !prof !0
arm5:
  store i8 7, ptr %state
  br label %join5
join5:
  ; %shared has a second predecessor, and %tail can leave to %done without
  ; passing %shared: neither successor is an arm.
  ; CHECK: br i1 %c6, label %shared, label %tail{{$}}
  br i1 %c6, label %shared, label %tail
tail:
  store i8 8, ptr %state
  br i1 %c0, label %shared, label %done
shared:
  store i8 9, ptr %state
  br label %done
done:
  ; The arm returns instead of rejoining.
  ; CHECK: br i1 %c7, label %leave, label %exit{{$}}
  br i1 %c7, label %leave, label %exit
leave:
  ret i32 1
exit:
  ret i32 0
}

; CHECK-LABEL: define i32 @__obelisk_region_kernel_0_0.__obelisk_eval_body_0
; CHECK-NOT: !prof
define i32 @__obelisk_region_kernel_0_0.__obelisk_eval_body_0(ptr %state, i1 %c0) {
entry:
  br i1 %c0, label %arm, label %join
arm:
  store i8 1, ptr %state
  br label %join
join:
  ret i32 0
}

!0 = !{!"branch_weights", i32 7, i32 3}
; CHECK-DAG: ![[GIVEN]] = !{!"branch_weights", i32 7, i32 3}
; CHECK-DAG: ![[UNLIKELY_TRUE]] = !{!"branch_weights", i32 1, i32 1000}
; CHECK-DAG: ![[UNLIKELY_FALSE]] = !{!"branch_weights", i32 1000, i32 1}
