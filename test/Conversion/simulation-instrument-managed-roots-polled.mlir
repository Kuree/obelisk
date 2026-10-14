// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-instrument-managed-roots)))' | FileCheck %s --check-prefix=EAGER
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-instrument-managed-roots{polled=true})))' | FileCheck %s --check-prefix=POLLED

// By default every allocation and call may collect, so each function with a
// handle live across one registers its roots at every activation. Under
// polled collection allocation never collects: only polls and calls that may
// reach one observe roots, and a function with neither registers nothing.
// An activation registers its record lazily, at the first collection point
// it reaches, so a path that reaches none pays only the record reset; the
// pop at each exit unregisters the record only if it was registered.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @polled_roots {
    llvm.mlir.global internal @__obelisk_current_context() : !llvm.ptr {
      %null = llvm.mlir.zero : !llvm.ptr
      llvm.return %null : !llvm.ptr
    }
    llvm.func @obelisk_rt_v1_gc_current_lane(!llvm.ptr) -> !llvm.ptr
    llvm.func @obelisk_rt_v1_gc_managed_root_range_push(
        !llvm.ptr, !llvm.ptr, !llvm.ptr, i64) -> i32
    llvm.func @obelisk_rt_v1_gc_managed_root_range_pop(
        !llvm.ptr, !llvm.ptr) -> i32
    llvm.func @obelisk_rt_v1_scheduler_fail(!llvm.ptr, i32)

    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "make"
    simulation.code_unit.decl 2 in 0 function hierarchy "allocate"
    simulation.code_unit.decl 3 in 0 function hierarchy "poll"
    simulation.code_unit.decl 4 in 0 function hierarchy "reach"
    simulation.code_unit.decl 5 in 0 initial hierarchy "tick"
    simulation.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }

    simulation.func private @make(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.class_handle<@Object>
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      simulation.return %object : !simulation.class_handle<@Object>
    }

    // A handle stays live across an allocation and a poll-free call.
    // EAGER-LABEL: simulation.func @allocate(
    // EAGER: schedule.gc.root_range.push
    // EAGER: simulation.class.root_bind
    // POLLED-LABEL: simulation.func @allocate(
    // POLLED-NOT: gc.root_range
    // POLLED-NOT: root_bind
    // POLLED: simulation.return
    simulation.func @allocate(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.class_handle<@Object>
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %first = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      %second = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      %third = simulation.call @make(%ctx) {simulation.gc_poll_free} :
          (!simulation.context) -> !simulation.class_handle<@Object>
      %kept = simulation.class.is_instance %first is @Object :
          !simulation.class_handle<@Object>
      simulation.return %first : !simulation.class_handle<@Object>
    }

    // A poll observes the handle live across it.
    // POLLED-LABEL: simulation.func @poll(
    // POLLED: %[[RECORD:.*]] = llvm.alloca {{.*}}native.managed_root_range_record
    // POLLED: llvm.store %{{.*}}, %[[RECORD]] {{{.*}}native.managed_root_activation_prologue}
    // POLLED-NOT: gc.root_range
    // POLLED: simulation.class.root_bind
    // POLLED-NEXT: schedule.gc.root_range.push %[[RECORD]] slots %{{.*}} count %{{.*}} lazy
    // POLLED-NEXT: runtime.status.from_bits
    // POLLED-NEXT: simulation.status.check
    // POLLED-NEXT: simulation.gc.safepoint
    // POLLED-NEXT: schedule.gc.root_range.pop %[[RECORD]]
    simulation.func @poll(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.class_handle<@Object>
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      simulation.gc.safepoint %ctx : !simulation.context
      simulation.return %object : !simulation.class_handle<@Object>
    }

    // So does a call that may reach a poll; a cold path that reaches no
    // collection point never registers the record.
    // POLLED-LABEL: simulation.func @reach(
    // POLLED: simulation.class.root_bind
    // POLLED-NOT: gc.root_range
    // POLLED: cf.cond_br
    // POLLED: schedule.gc.root_range.push %{{.*}} lazy
    // POLLED: simulation.status.check
    // POLLED-NEXT: simulation.call @poll
    // POLLED: schedule.gc.root_range.pop
    // POLLED: schedule.gc.root_range.pop
    simulation.func @reach(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.class_handle<@Object>
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      %cold = arith.constant false
      cf.cond_br %cold, ^reach, ^skip
    ^reach:
      %other = simulation.call @poll(%ctx) :
          (!simulation.context) -> !simulation.class_handle<@Object>
      simulation.return %object : !simulation.class_handle<@Object>
    ^skip:
      simulation.return %object : !simulation.class_handle<@Object>
    }

    // A process allocating once per resumption keeps its handle in the frame
    // across the delay and needs no activation roots.
    // EAGER-LABEL: simulation.func @tick(
    // EAGER: schedule.gc.root_range.push
    // POLLED-LABEL: simulation.func @tick(
    // POLLED-NOT: gc.root_range
    // POLLED: simulation.suspend.delay
    // POLLED-NOT: gc.root_range
    // POLLED: simulation.return
    simulation.func @tick(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 5 : i64, entry_kind = 1 : i32} {
      %held = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %fresh = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      %kept = simulation.class.is_instance %held is @Object :
          !simulation.class_handle<@Object>
      simulation.return
    }
  }
}
