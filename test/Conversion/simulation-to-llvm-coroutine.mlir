// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=ADJ
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes='verify,coro-early,coro-split<reuse-storage>,verify' | FileCheck %s --check-prefix=FRAME
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes='verify,coro-early,sroa,instcombine,simplifycfg,coro-split<reuse-storage>,coro-elide,coro-cleanup,sroa,instcombine,simplifycfg,dce,strip-dead-prototypes,verify' | FileCheck %s --check-prefix=SPLIT

!choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
], isTagged = false>
!record = !simulation.unpacked_struct<[
  #simulation.field<name = "byte", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "word", type = i16, ordinal = 1, packedOffset = 0>
]>
!handle_words = !simulation.unpacked_array<0 : 0 x !simulation.logic<8>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @coroutines {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.coroutines.delay_process.9000001"
    simulation.code_unit.decl 9000002 in 0 initial hierarchy "test.coroutines.all_waits.9000002"
    simulation.code_unit.decl 9000003 in 0 always hierarchy "test.coroutines.loop_wait.9000003"
    simulation.code_unit.decl 9000004 in 0 initial hierarchy "test.coroutines.reuse_continuation_slots.9000004"
    simulation.code_unit.decl 9000005 in 0 initial hierarchy "test.coroutines.maximum_continuation_id.9000005"
    simulation.code_unit.decl 9000006 in 0 initial hierarchy "test.coroutines.suspension_live_value.9000006"
    simulation.code_unit.decl 9000007 in 0 initial hierarchy "test.coroutines.plain_process.9000007"
    simulation.code_unit.decl 9000008 in 0 function hierarchy "test.coroutines.consume_ref.9000008"
    simulation.code_unit.decl 9000009 in 0 initial hierarchy "test.coroutines.capture_ref.9000009"
    simulation.code_unit.decl 9000010 in 0 function hierarchy "test.coroutines.consume_ref_with_status.9000010"
    simulation.code_unit.decl 9000011 in 0 initial hierarchy "test.coroutines.ref_lifetime.9000011"
    simulation.code_unit.decl 9000012 in 0 function hierarchy "test.coroutines.cfg_ref_lifetime.9000012"
    simulation.code_unit.decl 9000013 in 0 function hierarchy "test.coroutines.branched_ref_lifetime.9000013"
    simulation.code_unit.decl 9000014 in 0 function hierarchy "test.coroutines.wide_handle_indices.9000014"
    simulation.code_unit.decl 9000015 in 0 function hierarchy "test.coroutines.ordinary.9000015"
    simulation.code_unit.decl 9000016 in 0 function hierarchy "test.coroutines.union_extract.9000016"
    simulation.code_unit.decl 9000017 in 0 function hierarchy "test.coroutines.aggregate_insert.9000017"
    simulation.code_unit.decl 9000018 in 0 observer hierarchy "test.coroutines.shared_observer.9000018"
    simulation.code_unit.decl 9000019 in 0 initial hierarchy "test.coroutines.shared_observer_binding.9000019"
    simulation.scope.decl 0

    simulation.func @delay_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: !simulation.logic<5> {simulation.capture_kind = 2 : i32},
        %bit: !simulation.logic<1> {simulation.capture_kind = 2 : i32},
        %wide: !simulation.logic<65> {simulation.capture_kind = 2 : i32},
        %choose: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %delay = simulation.time.constant 5
      cf.br ^dispatch(%capture, %bit, %wide : !simulation.logic<5>, !simulation.logic<1>, !simulation.logic<65>)
    ^dispatch(%value: !simulation.logic<5>, %value_bit: !simulation.logic<1>,
              %value_wide: !simulation.logic<65>):
      cf.cond_br %choose, ^suspend_a, ^suspend_b
    ^suspend_a:
      simulation.suspend.delay %delay to ^resume(
        %value, %value_bit, %value_wide : !simulation.logic<5>,
        !simulation.logic<1>, !simulation.logic<65>)
    ^suspend_b:
      simulation.suspend.delay %delay to ^resume(
        %value, %value_bit, %value_wide : !simulation.logic<5>,
        !simulation.logic<1>, !simulation.logic<65>)
    ^resume(%resumed: !simulation.logic<5>,
            %resumed_bit: !simulation.logic<1>,
            %resumed_wide: !simulation.logic<65>):
      simulation.return
    }

    simulation.func @all_waits(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i8> {simulation.capture_kind = 1 : i32},
        %net: !simulation.net<i8> {simulation.capture_kind = 1 : i32},
        %event: !simulation.event {simulation.capture_kind = 1 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000002 : i64} {
      simulation.suspend.change %ref to ^edge : !simulation.ref<i8>
    ^edge:
      simulation.suspend.edge posedge %net to ^any : !simulation.net<i8>
    ^any:
      simulation.suspend.any %ref, %net edges [0, 1] to ^event_wait : !simulation.ref<i8>, !simulation.net<i8>
    ^event_wait:
      simulation.suspend.event %event to ^await
    ^await:
      simulation.suspend.await %process to ^join
    ^join:
      simulation.suspend.join all %process processes 1 to ^done : !simulation.process
    ^done:
      simulation.return
    }

    simulation.func @loop_wait(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i8> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 9000003 : i64} {
      cf.br ^header
    ^header:
      simulation.suspend.change %ref to ^header : !simulation.ref<i8>
    }

    simulation.func private @shared_observer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9000018 : i64,
                    schedule.observer_width = 1 : i32,
                    schedule.observer_four_state = false} {
      %false = arith.constant false
      simulation.return %false : i1
    }

    // One binding may be referenced by multiple source clauses. Lowering
    // serializes each occurrence but owns and erases the defining op once.
    simulation.func @shared_observer_binding(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i8> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000019 : i64} {
      %bound = simulation.observer.bind @shared_observer values(%ref : !simulation.ref<i8>) captures 0 : !simulation.observer<i1>
      %false = arith.constant false
      simulation.suspend.observe %bound, %bound, %false, %false conditions 0 edges [0, 0] indices [-1, -1] to ^resume : !simulation.observer<i1>, !simulation.observer<i1>, i1, i1
    ^resume:
      simulation.return
    }

    simulation.func @reuse_continuation_slots(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %left: i64 {simulation.capture_kind = 2 : i32},
        %right: i64 {simulation.capture_kind = 2 : i32},
        %choose: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000004 : i64} {
      %delay = simulation.time.constant 1
      cf.cond_br %choose, ^wait_left, ^wait_right
    ^wait_left:
      simulation.suspend.delay %delay to ^resume_left(%left : i64)
    ^wait_right:
      simulation.suspend.delay %delay to ^resume_right(%right : i64)
    ^resume_left(%value: i64):
      simulation.return
    ^resume_right(%right_value: i64):
      simulation.return
    }

    simulation.func @maximum_continuation_id(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000005 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^second
          {site = #schedule.continuation<id = 4294967295>}
    ^second:
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }

    simulation.func @suspension_live_value(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i32 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000006 : i64} {
      %one = arith.constant 1 : i32
      %live = arith.addi %capture, %one : i32
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resumed
    ^resumed:
      cf.br ^use
    ^use:
      %descriptor = arith.addi %live, %one : i32
      simulation.file.flush %ctx, %descriptor :
          (!simulation.context, i32) -> ()
      simulation.return
    }

    simulation.func @plain_process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000007 : i64} {
      %local = simulation.ref.alloc %capture : i64 -> !simulation.ref<i64>
      %loaded = simulation.ref.load %local : !simulation.ref<i64> -> i64
      simulation.return
    }

    simulation.func @consume_ref(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i64> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000008 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<i64> -> i64
      simulation.return
    }

    simulation.func @capture_ref(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i64> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000009 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<i64> -> i64
      simulation.return
    }

    simulation.func @consume_ref_with_status(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i64> {simulation.capture_kind = 1 : i32},
        %descriptor: i32 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000010 : i64} {
      %value = simulation.ref.load %ref : !simulation.ref<i64> -> i64
      simulation.file.flush %ctx, %descriptor :
          (!simulation.context, i32) -> ()
      simulation.return
    }

    simulation.func @ref_lifetime(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000011 : i64} {
      cf.br ^allocate
    ^allocate:
      %local = simulation.ref.alloc %capture : i64 -> !simulation.ref<i64>
      simulation.call @consume_ref(%ctx, %local) :
          (!simulation.context, !simulation.ref<i64>) -> ()
      %child = simulation.spawn @capture_ref(%ctx, %local) :
          !simulation.context, !simulation.ref<i64> -> !simulation.process
      simulation.return
    }

    simulation.func @cfg_ref_lifetime(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32},
        %repeat: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000012 : i64} {
      cf.br ^allocate
    ^allocate:
      %local = simulation.ref.alloc %capture : i64 -> !simulation.ref<i64>
      cf.br ^use(%local : !simulation.ref<i64>)
    ^use(%reference: !simulation.ref<i64>):
      %value = simulation.ref.load %reference : !simulation.ref<i64> -> i64
      cf.cond_br %repeat, ^allocate, ^done
    ^done:
      simulation.return
    }

    simulation.func @branched_ref_lifetime(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %capture: i64 {simulation.capture_kind = 2 : i32},
        %use_reference: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000013 : i64} {
      %local = simulation.ref.alloc %capture : i64 -> !simulation.ref<i64>
      cf.cond_br %use_reference, ^use(%local : !simulation.ref<i64>), ^done
    ^use(%reference: !simulation.ref<i64>):
      %value = simulation.ref.load %reference : !simulation.ref<i64> -> i64
      simulation.return
    ^done:
      simulation.return
    }

    simulation.func @wide_handle_indices(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 1 : i32},
        %array_ref: !simulation.ref<!handle_words> {simulation.capture_kind = 1 : i32},
        %driver: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 1 : i32},
        %array_driver: !simulation.driver<!handle_words> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000014 : i64} {
      %wide = arith.constant 18446744073709551616 : i129
      %wide_unknown = simulation.logic.constant 0 : i129, 1 : i129 : !simulation.logic<129>
      %ref_known = simulation.ref.dyn_extract %ref from %wide : (!simulation.ref<!simulation.logic<8>>, i129) -> !simulation.ref<!simulation.logic<4>>
      %ref_unknown = simulation.ref.dyn_extract %ref from %wide_unknown : (!simulation.ref<!simulation.logic<8>>, !simulation.logic<129>) -> !simulation.ref<!simulation.logic<4>>
      %driver_known = simulation.driver.dyn_extract %driver from %wide : (!simulation.driver<!simulation.logic<8>>, i129) -> !simulation.driver<!simulation.logic<4>>
      %driver_unknown = simulation.driver.dyn_extract %driver from %wide_unknown : (!simulation.driver<!simulation.logic<8>>, !simulation.logic<129>) -> !simulation.driver<!simulation.logic<4>>
      %ref_element_known = simulation.ref.array_element %array_ref[%wide] : (!simulation.ref<!handle_words>, i129) -> !simulation.ref<!simulation.logic<8>>
      %ref_element_unknown = simulation.ref.array_element %array_ref[%wide_unknown] : (!simulation.ref<!handle_words>, !simulation.logic<129>) -> !simulation.ref<!simulation.logic<8>>
      %driver_element_known = simulation.driver.array_element %array_driver[%wide] : (!simulation.driver<!handle_words>, i129) -> !simulation.driver<!simulation.logic<8>>
      %driver_element_unknown = simulation.driver.array_element %array_driver[%wide_unknown] : (!simulation.driver<!handle_words>, !simulation.logic<129>) -> !simulation.driver<!simulation.logic<8>>
      simulation.return
    }

    simulation.func @ordinary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i64 {simulation.capture_kind = 2 : i32}) -> i64
        attributes {entry_kind = 8 : i32, code_unit_id = 9000015 : i64} {
      simulation.return %value : i64
    }

    simulation.func @union_extract(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i16 {simulation.capture_kind = 2 : i32}) -> i8
        attributes {entry_kind = 8 : i32, code_unit_id = 9000016 : i64} {
      %union = simulation.union.construct %value as 1 : (i16) -> !choice
      %byte = simulation.union.extract %union[0] : (!choice) -> i8
      simulation.return %byte : i8
    }

    simulation.func @aggregate_insert(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: !record {simulation.capture_kind = 2 : i32},
        %replacement: i8 {simulation.capture_kind = 2 : i32}) -> !record
        attributes {entry_kind = 8 : i32, code_unit_id = 9000017 : i64} {
      %updated = simulation.aggregate.insert %replacement into %input[0] : (!record, i8) -> !record
      simulation.return %updated : !record
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
// CHECK-LABEL: llvm.func @shared_observer_binding.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1>
// CHECK-LABEL: llvm.func @reuse_continuation_slots.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1, 2>
// CHECK-SAME: obelisk.frame.size = 64 : i64
// CHECK-LABEL: llvm.func @maximum_continuation_id.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1, -1>
// CHECK-LABEL: llvm.func @suspension_live_value.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1>
// CHECK-SAME: obelisk.frame.size = 40 : i64
// CHECK-LABEL: llvm.func @plain_process
// CHECK-SAME: obelisk.native_scratch_size = 0 : i64
// CHECK: llvm.call @obelisk_rt_v1_native_state_alloc
// CHECK-NOT: llvm.intr.coro.
// CHECK-LABEL: llvm.func @plain_process.__obelisk_native_requirements
// CHECK-LABEL: llvm.func @plain_process.__obelisk_native_execute
// CHECK-LABEL: llvm.func @plain_process.__obelisk_native_destroy
// CHECK-LABEL: llvm.func @consume_ref
// CHECK: llvm.call @obelisk_rt_v1_native_state_release
// CHECK-LABEL: llvm.func @capture_ref(
// CHECK: llvm.call @obelisk_rt_v1_native_state_release
// CHECK-LABEL: llvm.func @consume_ref_with_status(
// CHECK: llvm.call @obelisk_rt_v1_native_state_release
// CHECK-LABEL: llvm.func @ref_lifetime
// CHECK: llvm.call @obelisk_rt_v1_native_state_alloc
// CHECK: llvm.call @obelisk_rt_v1_native_state_retain
// CHECK: llvm.call @consume_ref
// CHECK: llvm.call @obelisk_rt_v1_native_state_retain
// CHECK: llvm.call @obelisk_rt_v1_native_state_release
// CHECK-LABEL: llvm.func @cfg_ref_lifetime
// CHECK: llvm.call @obelisk_rt_v1_native_state_alloc
// CHECK: llvm.call @obelisk_rt_v1_native_state_release
// CHECK-NEXT: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NEXT: llvm.cond_br
// CHECK-LABEL: llvm.func @branched_ref_lifetime
// CHECK: llvm.call @obelisk_rt_v1_native_state_alloc
// CHECK-COUNT-2: llvm.call @obelisk_rt_v1_native_state_release
// CHECK-LABEL: llvm.func @wide_handle_indices
// Every wide index needs a signed-i64 round-trip check. The four-state cases
// above additionally exercise the unknown-plane guard (constant-folded here).
// CHECK-COUNT-8: llvm.icmp "eq"
// CHECK-LABEL: llvm.func @ordinary
// CHECK-SAME: obelisk.native_scratch_size = 0 : i64
// CHECK-LABEL: llvm.func @union_extract
// CHECK: llvm.trunc
// CHECK-LABEL: llvm.func @aggregate_insert
// CHECK: llvm.and
// CHECK-NOT: unrealized_conversion_cast
// CHECK-NOT: simulation.
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
