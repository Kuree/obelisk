// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s

// Under polled collection (`simulation.gc_polled`) an activation registers its
// managed-root record lazily: each collection point pushes it only when its
// runtime cookie is still zero, and each exit pops it only when it is
// registered. Entry and the resume shim reset the record; an ordinary edge into
// the resumed block keeps a registration made earlier in the activation.

module attributes {
  simulation.gc_polled,
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @managed_root_lazy {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.boundary"
    simulation.code_unit.decl 3 in 0 initial hierarchy "top.cold"
    simulation.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      cf.br ^resume(%object : !simulation.class_handle<@Object>)
    ^resume(%live: !simulation.class_handle<@Object>):
      simulation.gc.safepoint %ctx : !simulation.context
      %is_object = simulation.class.is_instance %live is @Object :
          !simulation.class_handle<@Object>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(
          %live : !simulation.class_handle<@Object>)
    }

    simulation.func @cold(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %reach: i1 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      cf.cond_br %reach, ^poll, ^done
    ^poll:
      simulation.gc.safepoint %ctx : !simulation.context
      cf.br ^done
    ^done:
      %is_object = simulation.class.is_instance %object is @Object :
          !simulation.class_handle<@Object>
      simulation.return
    }

    simulation.func @boundary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %activation = simulation.control.enter 1
      simulation.control.boundary %activation resume ^resume body ^body
    ^body:
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      simulation.gc.safepoint %ctx : !simulation.context
      %is_object = simulation.class.is_instance %object is @Object :
          !simulation.class_handle<@Object>
      simulation.return
    ^resume:
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @root.__talos_coro_ramp(
// CHECK: llvm.store %{{.*}}, %[[RECORD:[0-9]+]] {{{.*}}native.managed_root_activation_prologue}
// CHECK-NOT: llvm.call @talos_rt_v1_gc_managed_root_range_push
// CHECK: %[[COOKIE_ADDRESS:.*]] = llvm.getelementptr inbounds %[[RECORD]][24]
// CHECK-NEXT: %[[COOKIE:.*]] = llvm.load %[[COOKIE_ADDRESS]] {{.*}} -> i64
// CHECK: llvm.icmp "ne" %[[COOKIE]]
// CHECK-NEXT: llvm.cond_br %{{.*}}, ^[[JOIN:bb[0-9]+]](%{{.*}} : i32), ^[[PUSH:bb[0-9]+]]
// CHECK-NEXT: ^[[PUSH]]:
// CHECK: llvm.call @talos_rt_v1_gc_managed_root_range_push(%{{.*}}, %[[RECORD]],
// CHECK-NEXT: llvm.br ^[[JOIN]](
// CHECK: llvm.call @talos_rt_v1_gc_safepoint
// The pop before the delay runs only for a registered record.
// CHECK: llvm.getelementptr inbounds %[[RECORD]][24]
// CHECK: llvm.cond_br %{{.*}}, ^[[POP:bb[0-9]+]], ^[[SUSPEND:bb[0-9]+]]
// CHECK-NEXT: ^[[POP]]:
// CHECK: llvm.call @talos_rt_v1_gc_managed_root_range_pop(%{{.*}}, %[[RECORD]])
// CHECK: ^[[SUSPEND]]:
// CHECK: llvm.intr.coro.suspend
// Only the resume shim resets the record before rejoining the continuation.
// CHECK: llvm.store %{{.*}}, %[[RECORD]] {{{.*}}native.managed_root_activation_prologue}
// CHECK-NEXT: llvm.br ^bb

// A path that reaches no collection point never registers the record, and
// every exit, including each failure exit, pops only a registered record.
// CHECK-LABEL: llvm.func @cold(
// CHECK: llvm.cond_br %arg1, ^[[POLL:bb[0-9]+]], ^[[DONE:bb[0-9]+]]
// CHECK-NEXT: ^[[POLL]]:
// CHECK: llvm.call @talos_rt_v1_gc_managed_root_range_push
// CHECK: llvm.call @talos_rt_v1_gc_safepoint
// CHECK: ^[[DONE]]:
// CHECK-NOT: llvm.call @talos_rt_v1_gc_managed_root_range_push
// CHECK: llvm.call @talos_rt_v1_gc_managed_root_range_pop
