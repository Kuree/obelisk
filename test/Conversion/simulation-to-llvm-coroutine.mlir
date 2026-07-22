// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=ADJ
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes='verify,coro-early,coro-split<reuse-storage>,verify' | FileCheck %s --check-prefix=FRAME
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes='verify,coro-early,sroa,instcombine,simplifycfg,coro-split<reuse-storage>,coro-elide,coro-cleanup,sroa,instcombine,simplifycfg,dce,strip-dead-prototypes,verify' | FileCheck %s --check-prefix=SPLIT

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @coroutines {
    obelisk_sim.scope.decl 0

    obelisk_sim.func @delay_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %capture: !obelisk_sim.logic<5> {obelisk_sim.capture_kind = 2 : i32},
        %bit: !obelisk_sim.logic<1> {obelisk_sim.capture_kind = 2 : i32},
        %wide: !obelisk_sim.logic<65> {obelisk_sim.capture_kind = 2 : i32},
        %choose: i1 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 5
      cf.br ^dispatch(%capture, %bit, %wide : !obelisk_sim.logic<5>, !obelisk_sim.logic<1>, !obelisk_sim.logic<65>)
    ^dispatch(%value: !obelisk_sim.logic<5>, %value_bit: !obelisk_sim.logic<1>,
              %value_wide: !obelisk_sim.logic<65>):
      cf.cond_br %choose, ^suspend_a, ^suspend_b
    ^suspend_a:
      obelisk_sim.suspend.delay %delay to ^resume(
        %value, %value_bit, %value_wide : !obelisk_sim.logic<5>,
        !obelisk_sim.logic<1>, !obelisk_sim.logic<65>)
    ^suspend_b:
      obelisk_sim.suspend.delay %delay to ^resume(
        %value, %value_bit, %value_wide : !obelisk_sim.logic<5>,
        !obelisk_sim.logic<1>, !obelisk_sim.logic<65>)
    ^resume(%resumed: !obelisk_sim.logic<5>,
            %resumed_bit: !obelisk_sim.logic<1>,
            %resumed_wide: !obelisk_sim.logic<65>):
      obelisk_sim.return
    }

    obelisk_sim.func @all_waits(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32},
        %net: !obelisk_sim.net<i8> {obelisk_sim.capture_kind = 1 : i32},
        %event: !obelisk_sim.event {obelisk_sim.capture_kind = 1 : i32},
        %process: !obelisk_sim.process {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32} {
      obelisk_sim.suspend.change %ref to ^edge : !obelisk_sim.ref<i8>
    ^edge:
      obelisk_sim.suspend.edge posedge %net to ^any : !obelisk_sim.net<i8>
    ^any:
      obelisk_sim.suspend.any %ref, %net edges [0, 1] to ^event_wait : !obelisk_sim.ref<i8>, !obelisk_sim.net<i8>
    ^event_wait:
      obelisk_sim.suspend.event %event to ^await
    ^await:
      obelisk_sim.suspend.await %process to ^join
    ^join:
      obelisk_sim.suspend.join all %process processes 1 to ^done : !obelisk_sim.process
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @loop_wait(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32} {
      cf.br ^header
    ^header:
      obelisk_sim.suspend.change %ref to ^header : !obelisk_sim.ref<i8>
    }

    obelisk_sim.func @reuse_continuation_slots(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %left: i64 {obelisk_sim.capture_kind = 2 : i32},
        %right: i64 {obelisk_sim.capture_kind = 2 : i32},
        %choose: i1 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 1
      cf.cond_br %choose, ^wait_left, ^wait_right
    ^wait_left:
      obelisk_sim.suspend.delay %delay to ^resume_left(%left : i64)
    ^wait_right:
      obelisk_sim.suspend.delay %delay to ^resume_right(%right : i64)
    ^resume_left(%value: i64):
      obelisk_sim.return
    ^resume_right(%right_value: i64):
      obelisk_sim.return
    }

    obelisk_sim.func @maximum_continuation_id(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^second
          {site = #obelisk_sim.continuation<id = 4294967295>}
    ^second:
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @suspension_live_value(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %capture: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %one = arith.constant 1 : i32
      %live = arith.addi %capture, %one : i32
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resumed
    ^resumed:
      cf.br ^use
    ^use:
      %descriptor = arith.addi %live, %one : i32
      obelisk_sim.file.flush %ctx, %descriptor :
          (!obelisk_sim.context, i32) -> ()
      obelisk_sim.return
    }

    obelisk_sim.func @plain_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %capture: i64 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @ordinary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i64 {obelisk_sim.capture_kind = 2 : i32}) -> i64
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.return %value : i64
    }

  }
}

// CHECK-DAG: llvm.mlir.global external constant @delay_process.__obelisk_process_descriptor
// CHECK-DAG: llvm.mlir.global internal constant @delay_process.__obelisk_frame_layout
// CHECK-DAG: llvm.mlir.global internal constant @delay_process.__obelisk_continuations
// CHECK-DAG: llvm.mlir.global internal constant @delay_process.__obelisk_frame_fields
// CHECK-DAG: llvm.mlir.global external constant @plain_process.__obelisk_process_descriptor
// CHECK-LABEL: llvm.func @delay_process.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.alignment = 8 : i64
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1, 2>
// CHECK-SAME: passthrough = ["presplitcoroutine"]
// CHECK-DAG: llvm.intr.coro.id
// CHECK-DAG: llvm.intr.coro.size
// CHECK-DAG: llvm.intr.coro.align
// CHECK-DAG: llvm.intr.coro.begin
// CHECK-DAG: llvm.intr.coro.save
// CHECK-DAG: llvm.intr.coro.suspend
// CHECK-DAG: llvm.intr.coro.end
// CHECK-LABEL: llvm.func @delay_process.__obelisk_native_requirements
// CHECK-LABEL: llvm.func @delay_process.__obelisk_native_execute
// CHECK: llvm.intr.coro.resume
// CHECK-LABEL: llvm.func @delay_process.__obelisk_native_destroy
// CHECK: llvm.call_intrinsic "llvm.coro.destroy"
// CHECK-LABEL: llvm.func @all_waits.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1, 2, 3, 4, 5, 6>
// CHECK-SAME: obelisk.frame.size = 96 : i64
// CHECK-DAG: %[[ANY_WAIT:[0-9]+]] = llvm.getelementptr {{.*}}[32]
// CHECK-DAG: %[[ANY_EDGE_CHANGE_ADDR:[0-9]+]] = llvm.getelementptr %[[ANY_WAIT]][40]
// CHECK-DAG: llvm.store %{{[0-9]+}}, %[[ANY_EDGE_CHANGE_ADDR]]
// CHECK-DAG: %[[ANY_EDGE_POSEDGE_ADDR:[0-9]+]] = llvm.getelementptr {{.*}}[56]
// CHECK-DAG: llvm.store %{{[0-9]+}}, %[[ANY_EDGE_POSEDGE_ADDR]]
// CHECK-DAG: llvm.getelementptr {{.*}}[60]
// CHECK-LABEL: llvm.func @loop_wait.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1>
// CHECK-LABEL: llvm.func @reuse_continuation_slots.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1, 2>
// CHECK-SAME: obelisk.frame.size = 64 : i64
// CHECK-LABEL: llvm.func @maximum_continuation_id.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1, -1>
// CHECK-LABEL: llvm.func @suspension_live_value.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1>
// CHECK-SAME: obelisk.frame.size = 48 : i64
// CHECK-LABEL: llvm.func @plain_process
// CHECK-SAME: obelisk.native_scratch_size = 0 : i64
// CHECK-NOT: llvm.intr.coro.
// CHECK-LABEL: llvm.func @plain_process.__obelisk_native_requirements
// CHECK-LABEL: llvm.func @plain_process.__obelisk_native_execute
// CHECK-LABEL: llvm.func @plain_process.__obelisk_native_destroy
// CHECK-LABEL: llvm.func @ordinary
// CHECK-SAME: obelisk.native_scratch_size = 0 : i64
// CHECK-NOT: unrealized_conversion_cast
// CHECK-NOT: obelisk_sim.
// CHECK-NOT: arith.
// CHECK-NOT: cf.

// ADJ: llvm.intr.coro.save
// ADJ-NEXT: llvm.intr.coro.suspend

// FRAME: %delay_process.__obelisk_coro_ramp.Frame = type { ptr, ptr, ptr, i2 }

// SPLIT-NOT: @llvm.coro.
// SPLIT: @delay_process.__obelisk_coro_ramp.resumers = private constant
// SPLIT-LABEL: define void @delay_process.__obelisk_coro_ramp
// SPLIT-NOT: call ptr @malloc
// SPLIT-NOT: call void @free
// SPLIT: store i64 32, ptr %2
// SPLIT-NEXT: store i64 8, ptr %3
// SPLIT-NOT: call ptr @malloc
// SPLIT-NOT: call void @free
// SPLIT-LABEL: define i32 @plain_process.__obelisk_native_requirements
// SPLIT: store i64 0, ptr
// SPLIT: store i64 1, ptr
// SPLIT-LABEL: define internal fastcc void @delay_process.__obelisk_coro_ramp.resume
// SPLIT: store i32 2, ptr
// SPLIT: ret void
// SPLIT-LABEL: define internal fastcc void @delay_process.__obelisk_coro_ramp.destroy
// SPLIT-NOT: call ptr @malloc
// SPLIT-NOT: call void @free
// SPLIT-NOT: @llvm.coro.
